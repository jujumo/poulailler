#pragma once

// The debug-log fieldset shown at the bottom of the page. Its *definition*
// (not just its content) is compiled out entirely for a release build, so
// -D DEBUG_TRACES is what controls whether it exists at all, not just
// whether it has anything in it - see the {{DEBUG_LOG_SECTION}} placeholder
// below and WebPortal::buildIndexHtml().
#ifdef DEBUG_TRACES
constexpr const char kDebugLogSectionTemplate[] = R"HTML(<fieldset><legend>Debug log (this wake cycle)</legend>
<pre style='white-space:pre-wrap;word-break:break-all;max-height:300px;overflow:auto;background:#111;color:#0f0;padding:.5em;font-size:.75em;border-radius:4px'>{{DEBUG_LOG}}</pre>
</fieldset>)HTML";
#else
constexpr const char kDebugLogSectionTemplate[] = "";
#endif

// The config page, as plain HTML. WebPortal::buildIndexHtml() fills the
// {{PLACEHOLDER}} tokens in with the current config values and returns the
// result; nothing here is ever read from a filesystem, it's compiled
// straight into the firmware as a string constant.
constexpr const char kIndexPageTemplate[] = R"HTML(<!DOCTYPE html><html><head><meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>Coop Door Setup</title><style>
body{font-family:sans-serif;max-width:480px;margin:1em auto;padding:0 1em}
fieldset{margin-bottom:1em}label{display:block;margin-top:.5em}
input,select{width:100%;box-sizing:border-box;padding:.4em;margin-top:.2em}
button{padding:.6em 1em;margin-top:.5em}
.msg{background:#eef;padding:.5em;border-radius:4px;margin-bottom:1em}
.rtc-alert{background:#f8d7da;border:2px solid #b00020;color:#8b0000;font-weight:bold;padding:.7em;border-radius:4px;margin-bottom:1em}
.force{background:#fee}
.warn{display:none;background:#fee;border:1px solid #c00;color:#900;font-weight:bold;padding:.6em;border-radius:4px;margin-bottom:1em}
</style></head><body>
<h2>Coop Door Setup</h2>
<p>This configuration window is only open for 5 minutes after power-on. Power-cycle the board to reopen it.</p>
<div id='moveWarn' class='warn'>&#9888; The door may be moving right now &mdash; this page can stop responding for up to {{MOTOR_MAX_RUN_MS}}ms while it does. It will recover on its own once the move finishes.</div>
{{STATUS_BLOCK}}
<form method='POST' action='/settime' onsubmit='return fillTime(this)'>
<input type='hidden' name='y'><input type='hidden' name='mo'><input type='hidden' name='d'>
<input type='hidden' name='h'><input type='hidden' name='mi'><input type='hidden' name='s'>
<button type='submit'>Sync time from this device</button></form>
<form method='POST' action='/save'>
<fieldset><legend>Current RTC time</legend>
{{NOW_SUFFIX}}
<p>Local time: {{LOCAL_TIME}}</p>
<label>Timezone<select name='timezone'>{{TIMEZONE_OPTIONS}}</select></label>
<p>UTC time: {{UTC_TIME}}</p>
<p>Next sunrise (local): {{SUNRISE}}</p>
<p>Next sunrise (UTC): {{SUNRISE_UTC}}</p>
<p>Next sunset (local): {{SUNSET}}</p>
<p>Next sunset (UTC): {{SUNSET_UTC}}</p>
</fieldset>
<fieldset><legend>Location</legend>
<label>Latitude (-90..90)<input type='number' step='0.0001' name='lat' value='{{LAT}}'></label>
<label>Longitude (-180..180)<input type='number' step='0.0001' name='lon' value='{{LON}}'></label>
</fieldset>
<fieldset><legend>Door opens</legend>
<label><input type='radio' name='openMode' value='absolute'{{OPEN_ABS_CHECKED}}> At a fixed time</label>
<input type='time' name='openAbs' value='{{OPEN_ABS}}'>
<label><input type='radio' name='openMode' value='sun'{{OPEN_SUN_CHECKED}}> Relative to sunrise (minutes offset, +/-)</label>
<input type='text' inputmode='decimal' name='openSunOff' value='{{OPEN_SUN_OFF}}'>
<p>Door will open at {{OPEN_UTC}} UTC, hence {{OPEN_LOCAL}} local time.</p>
</fieldset>
<fieldset><legend>Door closes</legend>
<label><input type='radio' name='closeMode' value='absolute'{{CLOSE_ABS_CHECKED}}> At a fixed time</label>
<input type='time' name='closeAbs' value='{{CLOSE_ABS}}'>
<label><input type='radio' name='closeMode' value='sun'{{CLOSE_SUN_CHECKED}}> Relative to sunset (minutes offset, +/-)</label>
<input type='text' inputmode='decimal' name='closeSunOff' value='{{CLOSE_SUN_OFF}}'>
<p>Door will close at {{CLOSE_UTC}} UTC, hence {{CLOSE_LOCAL}} local time.</p>
</fieldset>
<fieldset><legend>Motor</legend>
<label>Open duration, ms<input type='number' name='motorOpenMs' value='{{MOTOR_OPEN_MS}}'></label>
<label>Close duration, ms<input type='number' name='motorCloseMs' value='{{MOTOR_CLOSE_MS}}'></label>
<label>Invert direction<input type='checkbox' name='motorInvertDirection'{{MOTOR_INVERT_CHECKED}}></label>
</fieldset>
<button type='submit'>Save settings</button></form>
<fieldset class='force'><legend>Debug</legend>
<form method='POST' action='/force-open' style='display:inline'><button type='submit'>Force Open</button></form>
<form method='POST' action='/force-close' style='display:inline'><button type='submit'>Force Close</button></form>
<form method='POST' action='/sleep' style='display:inline'><button type='submit'>Sleep now</button></form>
<form method='POST' action='/nap' style='display:inline'><button type='submit'>Nap and open</button></form>
<p>Last event: {{LAST_EVENT}}</p>
</fieldset>
{{DEBUG_LOG_SECTION}}
<script>
function fillTime(f){var d=new Date();
f.y.value=d.getUTCFullYear();f.mo.value=d.getUTCMonth()+1;f.d.value=d.getUTCDate();
f.h.value=d.getUTCHours();f.mi.value=d.getUTCMinutes();f.s.value=d.getUTCSeconds();
return true;}

// Heartbeat: an open/close (scheduled, or Force Open/Close below) blocks
// this whole sketch's loop for the move's duration (see CLAUDE.md), so a
// /ping that takes unusually long to answer is the only client-side signal
// that a move is actually under way - there's no way for the server to push
// that state, since it's the very thing that would be blocked from
// responding, and no way to know from a button click alone whether the
// move it triggers has actually started yet.
(function(){
    var warn = document.getElementById('moveWarn');
    var inFlight = false;
    function ping(){
        if (inFlight) return;
        inFlight = true;
        var slow = false;
        var timer = setTimeout(function(){ slow = true; warn.style.display = 'block'; }, 1500);
        fetch('/ping').then(function(){
            clearTimeout(timer);
            inFlight = false;
            if (slow) warn.style.display = 'none';
        }).catch(function(){
            clearTimeout(timer);
            inFlight = false;
        });
    }
    setInterval(ping, 3000);
})();
</script>
</body></html>)HTML";
