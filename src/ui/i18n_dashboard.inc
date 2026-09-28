// UI strings for the dashboard panels (dashboard.cc): header, nation /
// prefecture / district / PR bloc / review panels, tooltips, credits.
// Included into the table in i18n.cc; one entry per line:
//   {"key", {"日本語", "English", "繁體中文"}},
// Keys are prefixed "dash." so they never collide with i18n.cc / i18n_live.inc.

      // Header.
      {"dash.countdown_open", {"投票開始まで {0}", "Polls open in {0}", "距投票開始 {0}"}},
      {"dash.now_jst", {"現在 {0} JST", "Now {0} JST", "現在 {0} JST"}},
      // Left column.
      {"dash.offices.title", {"選挙の構成", "On the ballot", "選舉構成"}},
      {"dash.offices.seats", {"定数", "Seats", "席次"}},
      {"dash.offices.candidates", {"候補者", "Candidates", "候選人"}},
      {"dash.offices.total", {"定数 {0} · 候補者 {1}人（重複含む）", "{0} seats · {1} candidacies (incl. dual)", "總席次 {0} · 候選人 {1} 人次（含重複）"}},
      {"dash.area.bloc", {"比例 {0}", "PR bloc: {0}", "比例 {0}"}},
      {"dash.area.winner2024", {"前回当選", "2024 winner", "上屆當選"}},
      {"dash.area.not_running", {"不出馬", "Not running", "未參選"}},
      // Nation panel.
      {"dash.nation.title", {"衆議院 議席", "House seats", "眾議院席次"}},
      {"dash.nation.before", {"開票前 · 政党別の候補者数", "Before counting · candidates by party", "開票前 · 各黨候選人數"}},
      {"dash.nation.final", {"全議席確定", "All seats decided", "全部席次確定"}},
      {"dash.nation.majority_reached", {"{0}が単独過半数（{1}議席）", "{0} holds a majority alone ({1} seats)", "{0} 單獨過半（{1} 席）"}},
      {"dash.nation.supermajority_reached", {"{0}が単独で3分の2超（{1}議席）", "{0} holds two-thirds alone ({1} seats)", "{0} 單獨超過三分之二（{1} 席）"}},
      {"dash.nation.no_majority", {"単独過半数の政党なし", "No single-party majority", "無單一政黨過半"}},
      {"dash.nation.to_majority", {"{0}は過半数まであと{1}", "{0} is {1} short", "{0} 距過半數差 {1} 席"}},
      {"dash.nation.smd_progress", {"小選挙区 確定 {0}/{1}", "Districts decided {0}/{1}", "小選區確定 {0}/{1}"}},
      {"dash.nation.pr_progress", {"比例ブロック 確定 {0}/{1}", "PR blocs final {0}/{1}", "比例區完成 {0}/{1}"}},
      {"dash.nation.close", {"接戦の選挙区", "Closest districts", "差距最小的選區"}},
      {"dash.nation.cands_title", {"政党別 候補者数", "Candidates by party", "各黨候選人數"}},
      {"dash.col.party", {"政党", "Party", "政黨"}},
      {"dash.col.pr_share", {"比例得票率", "PR vote", "比例得票率"}},
      {"dash.col.pr_list", {"比例名簿", "PR list", "比例名單"}},
      {"dash.margin_votes", {"差 {0}票", "by {0}", "差 {0} 票"}},
      // Prefecture panel.
      {"dash.cands_n", {"{0}人", "{0} candidates", "{0} 人"}},
      {"dash.pref.bloc", {"{0}（定数{1}）", "{0} ({1} seats)", "{0}（應選 {1} 席）"}},
      {"dash.pref.bloc_hint", {"2キー：比例の詳細", "Key 2: PR details", "按 2：比例詳情"}},
      // District panel.
      {"dash.race.none", {"この地域の選挙区データなし", "No district data for this area", "此區無選區資料"}},
      {"dash.race.scope_district", {"小選挙区（定数1）", "Single-member district (1 seat)", "小選區（應選 1 席）"}},
      {"dash.race.scope_unit", {"{0} 開票分", "Votes counted in {0}", "{0} 開票結果"}},
      {"dash.race.progress", {"開票 {0}/{1} 単位 · {2}", "{0}/{1} units counted · {2}", "開票 {0}/{1} 單位 · {2}"}},
      {"dash.race.not_started", {"開票前", "Counting not started", "尚未開票"}},
      {"dash.race.totals", {"有効票 {0} · 有権者 {1}", "{0} valid votes · {1} eligible", "有效票 {0} · 選舉人 {1}"}},
      {"dash.race.order_note", {"開票前：届出順に表示", "Before counting: filing order", "開票前：依登記順序排列"}},
      {"dash.age", {"{0}歳", "age {0}", "{0} 歲"}},
      {"dash.sekihai", {"惜敗率 {0}", "Loss ratio {0}", "惜敗率 {0}"}},
      {"dash.chip.barred", {"復活不可", "PR barred", "不得復活"}},
      // PR bloc panel.
      {"dash.bloc.sub", {"比例代表 · ドント式 · 同順位は惜敗率順", "Proportional · D'Hondt · tied ranks by loss ratio", "比例代表 · 頓特法 · 同順位依惜敗率"}},
      {"dash.seats_n", {"定数 {0}", "{0} seats", "應選 {0} 席"}},
      {"dash.seats_unit", {"議席", "seats", "席"}},
      {"dash.votes_n", {"{0}票", "{0} votes", "{0} 票"}},
      {"dash.bloc.forfeited", {"{0}：名簿不足で{1}議席を他党へ", "{0} ran out of names: {1} seat(s) passed on", "{0} 名單不足：{1} 席轉給他黨"}},
      {"dash.bloc.legend", {"◆＝比例復活（小選挙区で落選し比例で当選）", "◆ = revived (lost the district, won a PR seat)", "◆＝比例復活（小選區落選、比例當選）"}},
      // Review panel.
      {"dash.review.sub", {"裁判官{0}人 · 総選挙と同日", "{0} justices · held with the election", "{0} 位法官 · 與大選同日"}},
      {"dash.review.marks", {"×印＝罷免を可とする · 無記入＝可としない", "× mark = dismiss · blank = retain", "打× = 應罷免 · 空白 = 不罷免"}},
      // Tooltip / legend.
      {"dash.tip.avg_margin", {"平均得票差", "Average margin", "平均差距"}},
      {"dash.tip.districts_led", {"リードしている選挙区", "Districts led", "領先選區"}},
      {"dash.legend.close", {"接戦", "Close", "膠著"}},
      {"dash.legend.safe", {"大差", "Safe", "大幅領先"}},
