// UI strings for the live layer and charts (dashboard_live.cc, charts.cc):
// banners, call-outs, feed, timeline, PiP, F2-F6 chart views.
// Included into the table in i18n.cc; one entry per line:
//   {"key", {"日本語", "English", "繁體中文"}},

      // Live layer: simulation / replay marks, 当確 banners and seal.
      {"live.sim_tag", {"模擬", "SIM", "模擬"}},
      {"live.replay_tag", {"再現", "REPLAY", "重播"}},
      {"live.live_tag", {"LIVE", "LIVE", "LIVE"}},
      {"live.banner_more", {"ほか{0}人", "+{0} more", "等 {0} 人"}},
      {"live.seal", {"当確", "CALLED", "當選"}},
      // F2 trend + seat projection.
      {"trend.title", {"{0} · 得票率の推移", "{0} · vote share over time", "{0} · 得票率走勢"}},
      {"trend.hint",
       {"← → 選挙区・比例ブロック切替 · クリックで他の選挙区へ", "← → switch district / PR bloc · click a race to switch",
        "← → 切換選區／比例區 · 點選其他選區"}},
      {"trend.seats_title",
       {"議席予測の推移（当確・確定＋リード中）", "Projected seats over the night (called + leading)",
        "席次預測走勢（確定＋領先）"}},
      // F3 hemicycle.
      {"seats.title", {"衆議院 {0}議席 · 過半数 {1}", "House of Representatives · {0} seats · majority {1}", "眾議院 {0} 席 · 過半數 {1}"}},
      {"live.seats_solid", {"当確・確定", "Called / decided", "確定"}},
      {"live.seats_breakdown", {"小選挙区{0} + 比例{1}", "{0} district + {1} PR", "小選區 {0} + 比例 {1}"}},
      {"live.seats_undecided", {"未定 {0}", "{0} undecided", "未定 {0}"}},
      // F4 margins.
      {"margins.title", {"接戦区 · 1位と2位の差が小さい順", "Closest districts first", "最接近的小選區排行"}},
      {"margins.hint",
       {"1位と2位の得票率差（ポイント）· クリックで地図へ", "Leader minus runner-up, in points · click to open on the map",
        "領先者與第二名的得票率差距 · 點選查看地圖"}},
      {"margins.more", {"ほか {0} 選挙区", "+{0} more districts", "另有 {0} 個選區"}},
      // F5 parties.
      {"parties.title", {"比例代表 · 政党別得票（全国）", "Proportional representation · votes by party", "比例代表 · 各政黨得票（全國）"}},
      {"parties.total", {"比例代表 有効票 {0} · 議席は小選挙区＋比例＝計", "Valid PR votes {0} · seats: district + PR = total",
                         "比例有效票 {0} · 席次：小選區＋比例＝合計"}},
      // F6 district grid.
      {"grid.title", {"全{0}選挙区 · 比例ブロック別", "All {0} districts by PR bloc", "全部 {0} 個小選區 · 依比例區"}},
      {"grid.legend",
       {"濃い色＝当確・確定 · 薄い色＝リード中 · 白い角＝当確 · クリックで地図へ",
        "Solid = called / decided · faded = leading · white corner = called · click to open",
        "實心＝確定 · 淡色＝領先 · 白角＝當選確定 · 點選查看地圖"}},
      {"grid.margin", {"差 {0} 票", "Margin {0}", "差距 {0} 票"}},
      {"grid.bloc_count", {"{0} 選挙区", "{0} districts", "{0} 個選區"}},
