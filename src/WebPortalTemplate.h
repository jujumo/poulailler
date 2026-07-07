#pragma once

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
.force{background:#fee}
</style></head><body>
<h2>Coop Door Setup</h2>
<p>This configuration window is only open for 5 minutes after power-on. Power-cycle the board to reopen it.</p>
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
<label>Run duration, ms<input type='number' name='motorRunMs' value='{{MOTOR_RUN_MS}}'></label>
<label>Invert direction<input type='checkbox' name='motorInvertDirection'{{MOTOR_INVERT_CHECKED}}></label>
</fieldset>
<button type='submit'>Save settings</button></form>
<fieldset class='force'><legend>Debug</legend>
<form method='POST' action='/force-open' style='display:inline'><button type='submit'>Force Open</button></form>
<form method='POST' action='/force-close' style='display:inline'><button type='submit'>Force Close</button></form>
<form method='POST' action='/sleep' style='display:inline'><button type='submit'>Sleep now</button></form>
<p>Last event: {{LAST_EVENT}}</p>
</fieldset>
<script>
function fillTime(f){var d=new Date();
f.y.value=d.getFullYear();f.mo.value=d.getMonth()+1;f.d.value=d.getDate();
f.h.value=d.getHours();f.mi.value=d.getMinutes();f.s.value=d.getSeconds();
return true;}
</script>
</body></html>)HTML";
