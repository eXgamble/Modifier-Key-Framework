#include "Hud.h"

#include "Settings.h"

// The vanilla prompt is two HUD fields: RolloverText ("Search\nDeer", HTML, grows down from its top)
// and RolloverButton_tf (the button image, placed left of the first line). Both are set by the HUD's
// SetCrosshairTarget, which the game calls. We shadow that function on the HUD instance with a native
// one: it runs the original, then adds what MKF needs: the modifier key's button right of the first
// line (the target has modifier actions), or, while the key is held, the extra labels above the first
// line, the Activate button moved down to its line, and one more button per extra line. Every button
// is filled by the HUD's own RefreshActivateButtonArt, so it looks exactly like the skin's Activate
// button. No swf is replaced: this works with any HUD that keeps vanilla's names.

namespace
{
	constexpr auto HUD_PATH = "_root.HUDMovieBaseInstance";
	constexpr auto ORIGINAL = "mkfOrigSetCrosshairTarget";


	struct Prompt
	{
		RE::ObjectRefHandle      target;
		bool                     marker = false;  // the modifier key's button after the label
		std::vector<std::string> labels;          // slot 2, slot 3 (modifier key held)
	};
	Prompt            prompt;  // written by the prompt hook, read by the HUD
	std::mutex        promptLock;
	std::atomic<bool> installed{ false };

	// The prompt's extras, if they belong to what the crosshair is on now
	Prompt ActivePrompt()
	{
		std::scoped_lock lock{ promptLock };
		const auto pick = RE::CrosshairPickData::GetSingleton();
		return pick && prompt.target && pick->GetActiveTarget() == prompt.target ? prompt : Prompt{};
	}

	std::string EscapeHtml(std::string_view a_text)
	{
		std::string out;
		for (const char c : a_text) {
			switch (c) {
			case '&': out += "&amp;"; break;
			case '<': out += "&lt;"; break;
			case '>': out += "&gt;"; break;
			default: out += c; break;
			}
		}
		return out;
	}

	double Number(const RE::GFxValue& a_object, const char* a_member)
	{
		RE::GFxValue value;
		a_object.GetMember(a_member, &value);
		return value.IsNumber() ? value.GetNumber() : 0.0;
	}

	struct LineMetrics
	{
		double x = 0.0;
		double width = 0.0;
		double height = 0.0;
	};

	LineMetrics GetLine(RE::GFxValue& a_text, std::uint32_t a_line)
	{
		RE::GFxValue metrics;
		RE::GFxValue arg(static_cast<double>(a_line));
		a_text.Invoke("getLineMetrics", &metrics, &arg, 1);
		return { Number(metrics, "x"), Number(metrics, "width"), Number(metrics, "height") + Number(metrics, "leading") };
	}

	// The button image the HUD shows for Activate right now ("E", "360_A", "PS3_A"): tells the device
	std::string ActivateArt(const RE::GFxValue& a_button)
	{
		RE::GFxValue html;
		a_button.GetMember("htmlText", &html);
		const std::string text = html.IsString() ? html.GetString() : "";
		std::string lower = text;
		std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		const auto src = lower.find("src=\"");
		if (src == std::string::npos) {
			return {};
		}
		const auto start = src + 5;
		const auto end = lower.find(".png", start);
		return end == std::string::npos ? std::string{} : text.substr(start, end - start);
	}

	// The image name for a game control on the device the HUD shows now, e.g. "R" or "360_X"
	std::string ArtFor(const RE::BSFixedString& a_control, const std::string& a_activateArt)
	{
		std::string lower = a_activateArt;
		std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		const bool playstation = lower.starts_with("ps3_");
		const bool gamepad = playstation || lower.starts_with("360_");
		const auto controlMap = RE::ControlMap::GetSingleton();
		RE::BSFixedString name;
		if (!controlMap || !controlMap->GetButtonNameFromUserEvent(a_control, gamepad ? RE::INPUT_DEVICE::kGamepad : RE::INPUT_DEVICE::kKeyboard, name) || !name.c_str()) {
			return {};
		}
		std::string art = name.c_str();
		if (playstation && art.starts_with("360_")) {
			art = "PS3_" + art.substr(4);
		}
		static std::set<std::string> logged;
		if (logged.insert(std::format("{}|{}", a_control.c_str(), art)).second) {
			logger::info("HUD: \"{}\" shows as {}.png (Activate: {}.png)", a_control.c_str(), art, a_activateArt);
		}
		return art;
	}

	// The image name for the modifier key on the device the HUD shows now, e.g. "Shift" or "360_LB"
	std::string ModifierArt(const std::string& a_activateArt)
	{
		std::string lower = a_activateArt;
		std::ranges::transform(lower, lower.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		const bool playstation = lower.starts_with("ps3_");
		const bool gamepad = playstation || lower.starts_with("360_");
		const auto devices = RE::BSInputDeviceManager::GetSingleton();
		RE::BSFixedString name;
		bool found = false;
		if (devices && gamepad && Settings::ModifierKeyGamepad() >= 0) {
			const auto mask = SKSE::InputMap::GamepadKeycodeToMask(static_cast<std::uint32_t>(Settings::ModifierKeyGamepad()));
			found = devices->GetButtonNameFromID(RE::INPUT_DEVICE::kGamepad, static_cast<std::int32_t>(mask), name);
		} else if (devices && !gamepad) {
			found = devices->GetButtonNameFromID(RE::INPUT_DEVICE::kKeyboard, static_cast<std::int32_t>(Settings::ModifierKey()), name);
		}
		if (!found || !name.c_str()) {
			return {};
		}
		std::string art = name.c_str();
		if (playstation && art.starts_with("360_")) {
			art = "PS3_" + art.substr(4);
		}
		static std::set<std::string> logged;
		if (logged.insert(art).second) {
			logger::info("HUD: the modifier key shows as {}.png (Activate: {}.png)", art, a_activateArt);
		}
		return art;
	}

	// An extra button field, made once next to the HUD's own Activate button
	RE::GFxValue Field(RE::GFxValue& a_button, const std::string& a_name)
	{
		RE::GFxValue parent;
		a_button.GetMember("_parent", &parent);
		const auto& name = a_name;
		RE::GFxValue field;
		parent.GetMember(name.c_str(), &field);
		if (field.IsUndefined() || field.IsNull()) {
			RE::GFxValue depth;
			parent.Invoke("getNextHighestDepth", &depth);
			std::array<RE::GFxValue, 6> args{
				RE::GFxValue(name.c_str()), depth,
				RE::GFxValue(Number(a_button, "_x")), RE::GFxValue(Number(a_button, "_y")),
				RE::GFxValue(Number(a_button, "_width")), RE::GFxValue(Number(a_button, "_height"))
			};
			parent.Invoke("createTextField", nullptr, args.data(), args.size());
			parent.GetMember(name.c_str(), &field);
			field.SetMember("html", RE::GFxValue(true));
			field.SetMember("selectable", RE::GFxValue(false));
			// the skin's own button field settings: alignment (the image hugs the text), wrapping
			for (const auto* member : { "multiline", "wordWrap", "embedFonts" }) {
				RE::GFxValue value;
				a_button.GetMember(member, &value);
				field.SetMember(member, value);
			}
			RE::GFxValue format;
			a_button.Invoke("getTextFormat", &format);
			field.Invoke("setNewTextFormat", nullptr, &format, 1);
			RE::GFxValue align;
			a_button.GetMember("autoSize", &align);
			field.SetMember("autoSize", align);
		}
		return field;
	}

	constexpr std::array<const char*, 3> FIELDS{ "mkfButton2", "mkfButton3", "mkfModifier" };

	void HideFields(RE::GFxValue& a_button)
	{
		RE::GFxValue parent;
		a_button.GetMember("_parent", &parent);
		for (const auto* name : FIELDS) {
			RE::GFxValue field;
			parent.GetMember(name, &field);
			if (!field.IsUndefined() && !field.IsNull()) {
				field.SetMember("_alpha", RE::GFxValue(0.0));
			}
		}
	}

	// Show a field where the HUD shows its own button (same alpha and HUD-mode visibility)
	void Place(RE::GFxValue& a_field, const RE::GFxValue& a_button, double a_x, double a_y)
	{
		a_field.SetMember("_x", RE::GFxValue(a_x));
		a_field.SetMember("_y", RE::GFxValue(a_y));
		a_field.SetMember("_alpha", RE::GFxValue(Number(a_button, "_alpha")));
		RE::GFxValue visible;
		a_button.GetMember("_visible", &visible);
		a_field.SetMember("_visible", visible);
	}

	std::string HtmlOf(const RE::GFxValue& a_field)
	{
		RE::GFxValue html;
		a_field.GetMember("htmlText", &html);
		return html.IsString() ? html.GetString() : "";
	}

	// The <IMG ...> tag in a field's HTML
	std::string ImageTag(const std::string& a_html)
	{
		const auto start = a_html.find("<IMG");
		const auto end = start == std::string::npos ? std::string::npos : a_html.find('>', start);
		return end == std::string::npos ? std::string{} : a_html.substr(start, end - start + 1);
	}

	// Fill a field with a button image exactly like the HUD's own Activate button: the HUD's
	// RefreshActivateButtonArt makes the image tag (the skin's size), and the Activate button's HTML
	// around it gives the skin's alignment and margins.
	bool ShowArt(RE::GFxValue& a_hud, RE::GFxValue& a_button, RE::GFxValue& a_field, const std::string& a_art, bool a_alignLeft = false)
	{
		a_hud.SetMember("RolloverButton_tf", a_field);
		RE::GFxValue arg(a_art.c_str());
		a_hud.Invoke("RefreshActivateButtonArt", nullptr, &arg, 1);
		a_hud.SetMember("RolloverButton_tf", a_button);

		const auto ours = ImageTag(HtmlOf(a_field));
		auto html = HtmlOf(a_button);
		const auto theirs = ImageTag(html);
		if (ours.empty() || theirs.empty()) {
			return false;  // no image of that name in this HUD
		}
		html.replace(html.find(theirs), theirs.size(), ours);
		if (a_alignLeft) {  // right of the text: hug it from the other side
			if (const auto p = html.find("<P ALIGN=\""); p != std::string::npos) {
				const auto start = p + 10;
				html.replace(start, html.find('"', start) - start, "LEFT");
			}
		}
		a_field.SetMember("htmlText", RE::GFxValue(html.c_str()));
		return true;
	}

	// The modifier key's button right of the label's line, as far from the text as the Activate
	// button is on the left; without an image for that key, the ini's text marker instead
	void ShowModifier(RE::GFxValue& a_hud, RE::GFxValue& a_text, RE::GFxValue& a_button, const RE::GFxValue& a_name)
	{
		auto field = Field(a_button, "mkfModifier");
		if (!ShowArt(a_hud, a_button, field, ModifierArt(ActivateArt(a_button)), true)) {
			if (!Settings::AlternateMarker().empty()) {
				std::string html = a_name.GetString();
				html.insert(std::min(html.find('\n'), html.size()), " " + EscapeHtml(Settings::AlternateMarker()));
				std::array<RE::GFxValue, 2> setArgs{ RE::GFxValue(html.c_str()), RE::GFxValue(true) };
				a_text.Invoke("SetText", nullptr, setArgs.data(), setArgs.size());
			}
			return;
		}
		const auto line = GetLine(a_text, 0);
		const double textX = Number(a_text, "_x") + line.x;
		const double gap = textX - (Number(a_button, "_x") + Number(a_button, "_width"));
		Place(field, a_button, textX + line.width + gap, Number(a_button, "_y"));
	}

	void Apply(RE::GFxValue& a_hud, bool a_activate, const RE::GFxValue& a_name)
	{
		RE::GFxValue text, button;
		a_hud.GetMember("RolloverText", &text);
		a_hud.GetMember("RolloverButton_tf", &button);
		if (!text.IsDisplayObject() && !text.IsObject()) {
			return;
		}

		// Where the HUD keeps its button; we move it while a stack shows and put it back after
		RE::GFxValue moved;
		a_hud.GetMember("mkfButtonMoved", &moved);
		const bool wasMoved = moved.IsBool() && moved.GetBool();
		if (!wasMoved) {
			a_hud.SetMember("mkfButtonBaseY", RE::GFxValue(Number(button, "_y")));
		}
		const double baseY = [&] { RE::GFxValue y; a_hud.GetMember("mkfButtonBaseY", &y); return y.IsNumber() ? y.GetNumber() : Number(button, "_y"); }();

		const auto active = a_activate && a_name.IsString() ? ActivePrompt() : Prompt{};
		const auto& labels = active.labels;
		HideFields(button);
		if (labels.empty()) {
			if (wasMoved) {
				button.SetMember("_y", RE::GFxValue(baseY));
				a_hud.SetMember("mkfButtonMoved", RE::GFxValue(false));
			}
			if (active.marker) {
				ShowModifier(a_hud, text, button, a_name);
			}
			return;
		}

		// The HUD placed the button for the first line of its own text: remember that offset
		const double buttonX = Number(button, "_x");
		const double firstLineX = GetLine(text, 0).x;

		// Extra lines on top, highest slot first: "Y line\nX line\n" + "<Activate line>\n<name>"
		std::string html;
		for (auto it = labels.rbegin(); it != labels.rend(); ++it) {
			html += EscapeHtml(*it) + "\n";
		}
		html += a_name.GetString();
		std::array<RE::GFxValue, 2> setArgs{ RE::GFxValue(html.c_str()), RE::GFxValue(true) };
		text.Invoke("SetText", nullptr, setArgs.data(), setArgs.size());

		const auto extra = static_cast<std::uint32_t>(labels.size());
		const std::string activateArt = ActivateArt(button);
		double top = 0.0;
		for (std::uint32_t line = 0; line <= extra; ++line) {
			const auto metrics = GetLine(text, line);
			const double x = buttonX + (metrics.x - firstLineX);
			if (line == extra) {
				button.SetMember("_x", RE::GFxValue(x));
				button.SetMember("_y", RE::GFxValue(baseY + top));
			} else {
				const std::size_t slot = extra - 1 - line;  // top line = highest slot
				auto field = Field(button, FIELDS[slot]);
				ShowArt(a_hud, button, field, ArtFor(Settings::SlotControl(slot + 2), activateArt));
				Place(field, button, x, baseY + top);
			}
			top += metrics.height;
		}
		a_hud.SetMember("mkfButtonMoved", RE::GFxValue(true));
	}

	class SetCrosshairTarget : public RE::GFxFunctionHandler
	{
	public:
		void Call(Params& a_params) override
		{
			if (!a_params.thisPtr) {
				return;
			}
			RE::GFxValue hud = *a_params.thisPtr;
			hud.Invoke(ORIGINAL, a_params.retVal, a_params.args, a_params.argCount);
			const bool activate = a_params.argCount > 0 && a_params.args[0].IsBool() && a_params.args[0].GetBool();
			const RE::GFxValue name = a_params.argCount > 1 ? a_params.args[1] : RE::GFxValue{};
			Apply(hud, activate, name);
		}
	};

	void Install()
	{
		const auto ui = RE::UI::GetSingleton();
		const auto menu = ui ? ui->GetMenu(RE::HUDMenu::MENU_NAME) : nullptr;
		const auto movie = menu ? menu->uiMovie.get() : nullptr;
		RE::GFxValue hud;
		if (!movie || !movie->GetVariable(&hud, HUD_PATH)) {
			return;
		}
		RE::GFxValue existing;
		hud.GetMember(ORIGINAL, &existing);
		if (!existing.IsUndefined()) {
			installed = true;
			return;
		}
		RE::GFxValue original;
		hud.GetMember("SetCrosshairTarget", &original);
		if (original.IsUndefined()) {
			logger::warn("HUD: no SetCrosshairTarget on {}, the action stack can't be shown", HUD_PATH);
			return;
		}
		static auto* handler = new SetCrosshairTarget();  // lives for the session
		RE::GFxValue wrapper;
		movie->CreateFunction(&wrapper, handler);
		hud.SetMember(ORIGINAL, original);
		hud.SetMember("SetCrosshairTarget", wrapper);
		installed = true;
		logger::info("HUD: action stack installed");
	}

	class MenuSink : public RE::BSTEventSink<RE::MenuOpenCloseEvent>
	{
	public:
		static MenuSink* GetSingleton()
		{
			static MenuSink singleton;
			return &singleton;
		}

		RE::BSEventNotifyControl ProcessEvent(const RE::MenuOpenCloseEvent* a_event, RE::BSTEventSource<RE::MenuOpenCloseEvent>*) override
		{
			if (a_event && a_event->opening && a_event->menuName == RE::HUDMenu::MENU_NAME) {
				SKSE::GetTaskInterface()->AddUITask(Install);
			}
			return RE::BSEventNotifyControl::kContinue;
		}
	};
}

namespace Hud
{
	void Register()
	{
		if (const auto ui = RE::UI::GetSingleton()) {
			ui->AddEventSink<RE::MenuOpenCloseEvent>(MenuSink::GetSingleton());
		}
	}

	void SetPrompt(RE::TESObjectREFR* a_target, bool a_marker, std::vector<std::string> a_labels)
	{
		if (!installed) {
			SKSE::GetTaskInterface()->AddUITask(Install);  // in case the HUD opened before we were watching
		}
		std::scoped_lock lock{ promptLock };
		if (!a_target || (!a_marker && a_labels.empty())) {
			prompt = {};
			return;
		}
		prompt.target = a_target->GetHandle();
		prompt.marker = a_marker && a_labels.empty();
		prompt.labels = std::move(a_labels);
	}

	void ClearPrompt()
	{
		std::scoped_lock lock{ promptLock };
		prompt = {};
	}

	bool Ready()
	{
		return installed;
	}
}
