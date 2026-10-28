#pragma once

// The debug-log fieldset shown at the bottom of the page. Its definition
// is compiled out entirely for a release build.
#ifdef DEBUG_TRACES
constexpr const char kDebugLogSectionTemplate[] = R"HTML(
<fieldset>
    <legend>Debug log (this wake cycle)</legend>
    <pre style='white-space:pre-wrap;word-break:break-all;max-height:300px;overflow:auto;background:#111;color:#0f0;padding:.5em;font-size:.75em;border-radius:4px'>{{DEBUG_LOG}}</pre>
</fieldset>
)HTML";
#else
constexpr const char kDebugLogSectionTemplate[] = "";
#endif

// The configuration page, compiled directly into the firmware.
// WebPortal::buildIndexHtml() replaces the {{PLACEHOLDER}} tokens.
constexpr const char kIndexPageTemplate[] = R"HTML(
<!DOCTYPE html>
<html>
<head>
    <meta charset='utf-8'>
    <meta name='viewport' content='width=device-width,initial-scale=1'>
    <title>Coop Door Setup</title>

    <style>
        body {
            font-family: sans-serif;
            max-width: 480px;
            margin: 1em auto;
            padding: 0 1em;
        }

        fieldset {
            margin-bottom: 1em;
        }

        label {
            display: block;
            margin-top: .5em;
        }

        input,
        select {
            width: 100%;
            box-sizing: border-box;
            padding: .4em;
            margin-top: .2em;
        }

        button {
            padding: .6em 1em;
            margin-top: .5em;
        }

        .msg {
            background: #eef;
            padding: .5em;
            border-radius: 4px;
            margin-bottom: 1em;
        }

        .rtc-alert {
            background: #f8d7da;
            border: 2px solid #b00020;
            color: #8b0000;
            font-weight: bold;
            padding: .7em;
            border-radius: 4px;
            margin-bottom: 1em;
        }

        .force {
            background: #fee;
        }

        .warn {
            display: none;
            background: #fee;
            border: 1px solid #c00;
            color: #900;
            font-weight: bold;
            padding: .6em;
            border-radius: 4px;
            margin-bottom: 1em;
        }

        .preview {
            font-weight: bold;
        }

        .hint {
            color: #555;
            font-size: .9em;
        }
    </style>
</head>

<body>
    <h2>Coop Door Setup</h2>

    <p>Compiled: {{COMPILE_TIME}}</p>

    <p>
        This configuration window is only open for 5 minutes after power-on.
        Power-cycle the board to reopen it.
    </p>

    <div id='move_warn' class='warn'>
        &#9888; The door may be moving right now &mdash; this page can stop
        responding for up to {{MOTOR_MAX_RUN_MS}}ms while it does. It will
        recover on its own once the move finishes.
    </div>

    {{STATUS_BLOCK}}

    <form method='POST' action='/settime' onsubmit='return fill_time(this)'>
        <input type='hidden' name='y'>
        <input type='hidden' name='mo'>
        <input type='hidden' name='d'>
        <input type='hidden' name='h'>
        <input type='hidden' name='mi'>
        <input type='hidden' name='s'>

        <button type='submit'>Sync time from this device</button>
    </form>

    <form method='POST' action='/save'>
        <fieldset>
            <legend>Current RTC time</legend>
            {{NOW_SUFFIX}}
            <p>UTC time  : {{UTC_TIME}}</p>
            <p>Local time: <strong>{{LOCAL_TIME}}</strong> {{TZ_ABBREV}}
               (DST active: {{DST_ACTIVE}})</p>
            <label> Time zone (DST handled automatically):
                <select name='timezone' id='timezone' onchange='tz_changed()'>
                    <option value='CET-1CEST,M3.5.0,M10.5.0/3'>Europe/Paris, Berlin, Madrid, Rome (CET/CEST)</option>
                    <option value='GMT0BST,M3.5.0/1,M10.5.0'>Europe/London, Dublin (GMT/BST)</option>
                    <option value='WET0WEST,M3.5.0/1,M10.5.0'>Europe/Lisbon (WET/WEST)</option>
                    <option value='EET-2EEST,M3.5.0/3,M10.5.0/4'>Europe/Athens, Helsinki, Kyiv (EET/EEST)</option>
                    <option value='MSK-3'>Europe/Moscow (MSK)</option>
                    <option value='EST5EDT,M3.2.0,M11.1.0'>America/New_York (EST/EDT)</option>
                    <option value='CST6CDT,M3.2.0,M11.1.0'>America/Chicago (CST/CDT)</option>
                    <option value='MST7MDT,M3.2.0,M11.1.0'>America/Denver (MST/MDT)</option>
                    <option value='PST8PDT,M3.2.0,M11.1.0'>America/Los_Angeles (PST/PDT)</option>
                    <option value='AEST-10AEDT,M10.1.0,M4.1.0/3'>Australia/Sydney (AEST/AEDT)</option>
                    <option value='JST-9'>Asia/Tokyo (JST)</option>
                    <option value='UTC0'>UTC (no DST)</option>
                    <option value='custom'>Custom POSIX TZ string...</option>
                </select>
            </label>
            <label id='timezone_custom_label'> POSIX TZ string:
                <input type='text' name='timezone_custom' id='timezone_custom'
                       maxlength='47' placeholder='CET-1CEST,M3.5.0,M10.5.0/3'
                       value='{{TIMEZONE}}'>
            </label>
            <button type='button' onclick='detect_timezone()'>Detect from this device</button>
            <p id='tz_detected' class='hint'></p>
        </fieldset>

        <fieldset>
            <legend>Sun ephemeris</legend>
            <label>
                Latitude (-90..90)
                <input type='number' step='0.0001' name='latitude' value='{{LATITUDE}}'>
            </label>

            <label>
                Longitude (-180..180)
                <input type='number' step='0.0001' name='longitude' value='{{LONGITUDE}}'>
            </label>

            <p>
                Today's sunrise:
                <br>
                local: <strong><span id='sunrise_local'>{{SUNRISE_LOCAL}}</span></strong>
                <br>
                UTC: <span id='sunrise_utc'>{{SUNRISE_UTC}}</span>
            </p>

            <p>
                Today's sunset:
                <br>
                local: <strong><span id='sunset_local'>{{SUNSET_LOCAL}}</span></strong>
                <br>
                UTC: <span id='sunset_utc'>{{SUNSET_UTC}}</span>
            </p>
        </fieldset>

        <fieldset>
            <legend>Door opens</legend>

            <p>
                The door is currently set to open at
                {{OPEN_TIMEOFDAY_LOCAL}}
                ({{OPEN_TIMEOFDAY_UTC}} UTC).
                Changes made here take effect only after you save settings.
            </p>

            <label>
                <input type='radio' name='open_mode' value='timeofday' {{OPEN_TIMEOFDAY_CHECKED}}>
                At a fixed time
            </label>

            <input type='time' name='open_timeofday_local' value='{{OPEN_TIMEOFDAY_LOCAL}}'>

            <label>
                <input type='radio' name='open_mode' value='sun' {{OPEN_SUN_CHECKED}}>
                Relative to sunrise (minutes offset, +/-)
            </label>

            <input type='number' min='-720' max='720' step='1' name='open_sun_offset' value='{{OPEN_SUN_OFFSET}}'>
            Sunrise local time: <strong><span id='sunrise_local'>{{SUNRISE_LOCAL}}</span></strong>

        </fieldset>

        <fieldset>
            <legend>Door closes</legend>

            <p>
                The door is currently set to close at
                {{CLOSE_TIMEOFDAY_LOCAL}}
                ({{CLOSE_TIMEOFDAY_UTC}} UTC).
                Changes made here take effect only after you save settings.
            </p>

            <label>
                <input type='radio' name='close_mode' value='timeofday' {{CLOSE_TIMEOFDAY_CHECKED}}>
                At a fixed time
            </label>

            <input type='time' name='close_timeofday_local' value='{{CLOSE_TIMEOFDAY_LOCAL}}'>

            <label>
                <input type='radio' name='close_mode' value='sun' {{CLOSE_SUN_CHECKED}}>
                Relative to sunset (minutes offset, +/-)
            </label>

            <input type='number' min='-720' max='720' step='1' name='close_sun_offset' value='{{CLOSE_SUN_OFFSET}}'>

            Sunset local time: <strong><span id='sunset_local'>{{SUNSET_LOCAL}}</span></strong>
        </fieldset>

        <fieldset>
            <legend>Motor</legend>

            <label>
                Open duration, ms
                <input type='number' name='motor_open_duration_ms' value='{{MOTOR_OPEN_DURATION_MS}}'>
            </label>

            <label>
                Close duration, ms
                <input type='number' name='motor_close_duration_ms' value='{{MOTOR_CLOSE_DURATION_MS}}'>
            </label>

            <label>
                <input type='checkbox' name='motor_invert_direction' {{MOTOR_INVERT_DIRECTION_CHECKED}}>
                Invert direction
            </label>
        </fieldset>
        <fieldset class='config'>
            <legend>config</legend>
            <button type='submit'>Save settings</button>
        </fieldset>
    </form>

    <fieldset class='door'>
        <legend>Door</legend>
        <form method='POST' action='/open' style='display:inline'>
            <button type='submit'>open</button>
        </form>
        <form method='POST' action='/close' style='display:inline'>
            <button type='submit'>close</button>
        </form>
    </fieldset>

    <fieldset class='force'>
        <legend>Debug</legend>
        <form method='POST' action='/reload' style='display:inline'>
            <button type='submit'>Reload config</button>
        </form>
        <form method='POST' action='/reset' style='display:inline'>
            <button type='submit'>Reset schedule</button>
        </form>
        <br>
        <form method='POST' action='/update' style='display:inline'>
            <button type='submit'>Update schedule</button>
        </form>
        <br>
        <form method='POST' action='/force-open' style='display:inline'>
            <button type='submit'>Plan open now, then wifi</button>
        </form>
        <br>
        <form method='POST' action='/force-close' style='display:inline'>
            <button type='submit'>Plan close now, then wifi</button>
        </form>
        <br>
        <form method='POST' action='/nap' style='display:inline'>
            <button type='submit'>Nap for 20s</button>
        </form>
        <br>
        <form method='POST' action='/sleep' style='display:inline'>
            <button type='submit'>Sleep now</button>
        </form>

        <p>Scheduled events:<hr> {{SCHEDULED_EVENTS}}</p>
    </fieldset>

    {{DEBUG_LOG_SECTION}}

    <script>
        // IANA zone names (as reported by the browser) -> POSIX TZ strings
        // offered in the <select>. Anything else falls back to a fixed
        // offset rule built from getTimezoneOffset().
        var kIanaToPosix = {
            'Europe/Paris': 'CET-1CEST,M3.5.0,M10.5.0/3',
            'Europe/Berlin': 'CET-1CEST,M3.5.0,M10.5.0/3',
            'Europe/Madrid': 'CET-1CEST,M3.5.0,M10.5.0/3',
            'Europe/Rome': 'CET-1CEST,M3.5.0,M10.5.0/3',
            'Europe/Brussels': 'CET-1CEST,M3.5.0,M10.5.0/3',
            'Europe/Amsterdam': 'CET-1CEST,M3.5.0,M10.5.0/3',
            'Europe/Zurich': 'CET-1CEST,M3.5.0,M10.5.0/3',
            'Europe/Vienna': 'CET-1CEST,M3.5.0,M10.5.0/3',
            'Europe/Prague': 'CET-1CEST,M3.5.0,M10.5.0/3',
            'Europe/Warsaw': 'CET-1CEST,M3.5.0,M10.5.0/3',
            'Europe/Stockholm': 'CET-1CEST,M3.5.0,M10.5.0/3',
            'Europe/Oslo': 'CET-1CEST,M3.5.0,M10.5.0/3',
            'Europe/Copenhagen': 'CET-1CEST,M3.5.0,M10.5.0/3',
            'Europe/Budapest': 'CET-1CEST,M3.5.0,M10.5.0/3',
            'Europe/London': 'GMT0BST,M3.5.0/1,M10.5.0',
            'Europe/Dublin': 'GMT0BST,M3.5.0/1,M10.5.0',
            'Europe/Lisbon': 'WET0WEST,M3.5.0/1,M10.5.0',
            'Atlantic/Canary': 'WET0WEST,M3.5.0/1,M10.5.0',
            'Europe/Athens': 'EET-2EEST,M3.5.0/3,M10.5.0/4',
            'Europe/Helsinki': 'EET-2EEST,M3.5.0/3,M10.5.0/4',
            'Europe/Kyiv': 'EET-2EEST,M3.5.0/3,M10.5.0/4',
            'Europe/Kiev': 'EET-2EEST,M3.5.0/3,M10.5.0/4',
            'Europe/Bucharest': 'EET-2EEST,M3.5.0/3,M10.5.0/4',
            'Europe/Sofia': 'EET-2EEST,M3.5.0/3,M10.5.0/4',
            'Europe/Moscow': 'MSK-3',
            'America/New_York': 'EST5EDT,M3.2.0,M11.1.0',
            'America/Toronto': 'EST5EDT,M3.2.0,M11.1.0',
            'America/Detroit': 'EST5EDT,M3.2.0,M11.1.0',
            'America/Chicago': 'CST6CDT,M3.2.0,M11.1.0',
            'America/Denver': 'MST7MDT,M3.2.0,M11.1.0',
            'America/Los_Angeles': 'PST8PDT,M3.2.0,M11.1.0',
            'America/Vancouver': 'PST8PDT,M3.2.0,M11.1.0',
            'Australia/Sydney': 'AEST-10AEDT,M10.1.0,M4.1.0/3',
            'Australia/Melbourne': 'AEST-10AEDT,M10.1.0,M4.1.0/3',
            'Asia/Tokyo': 'JST-9',
            'UTC': 'UTC0',
            'Etc/UTC': 'UTC0'
        };

        function tz_select_has(value) {
            var select = document.getElementById('timezone');
            for (var i = 0; i < select.options.length; i++) {
                if (select.options[i].value === value) {
                    return true;
                }
            }
            return false;
        }

        function tz_changed() {
            var select = document.getElementById('timezone');
            var custom_label = document.getElementById('timezone_custom_label');
            custom_label.style.display =
                select.value === 'custom' ? 'block' : 'none';
        }

        function set_timezone(posix) {
            var select = document.getElementById('timezone');
            var custom = document.getElementById('timezone_custom');
            if (tz_select_has(posix)) {
                select.value = posix;
            } else {
                select.value = 'custom';
            }
            custom.value = posix;
            tz_changed();
        }

        // Fixed-offset POSIX rule from the browser's current UTC offset,
        // e.g. UTC+2 -> "<+02>-2" (POSIX offsets are west-positive).
        function posix_fixed_offset(minutes_east) {
            var sign_name = minutes_east < 0 ? '-' : '+';
            var sign_rule = minutes_east < 0 ? '' : '-';
            var abs = Math.abs(minutes_east);
            var hh = Math.floor(abs / 60);
            var mm = abs % 60;
            var name = sign_name + (hh < 10 ? '0' : '') + hh + (mm ? (mm < 10 ? '0' : '') + mm : '');
            var rule = sign_rule + hh + (mm ? ':' + (mm < 10 ? '0' : '') + mm : '');
            return '<' + name + '>' + rule;
        }

        function detect_timezone() {
            var iana = '';
            try {
                iana = Intl.DateTimeFormat().resolvedOptions().timeZone || '';
            } catch (e) {}
            var minutes_east = -new Date().getTimezoneOffset();
            var posix = kIanaToPosix[iana];
            var note = document.getElementById('tz_detected');
            if (posix) {
                set_timezone(posix);
                note.textContent = 'Your device reports ' + iana
                    + ' - selected. Save settings to apply.';
            } else {
                set_timezone(posix_fixed_offset(minutes_east));
                note.textContent = 'Your device reports ' + (iana || 'an unknown zone')
                    + ' (UTC' + (minutes_east >= 0 ? '+' : '') + (minutes_east / 60)
                    + '), not in the list: a fixed offset without DST was filled '
                    + 'in. Edit the POSIX TZ string if you need a DST rule, '
                    + 'then save settings.';
            }
        }

        // Preselect the <select> from the firmware's current setting (the
        // custom input's value attribute, HTML-decoded by the browser).
        set_timezone(document.getElementById('timezone_custom').value);

        function fill_time(form) {
            var date = new Date();

            form.y.value = date.getUTCFullYear();
            form.mo.value = date.getUTCMonth() + 1;
            form.d.value = date.getUTCDate();
            form.h.value = date.getUTCHours();
            form.mi.value = date.getUTCMinutes();
            form.s.value = date.getUTCSeconds();

            return true;
        }

        function parse_clock(value) {
            // Accept both HH:MM and HH:MM:SS, because the server-side
            // DateTime formatter may include seconds.
            var match = value.trim().match(
                /^(\d{1,2}):(\d{2})(?::\d{2})?$/
            );

            if (!match) {
                return null;
            }

            var hours = Number(match[1]);
            var minutes = Number(match[2]);

            return hours < 24 && minutes < 60
                ? hours * 60 + minutes
                : null;
        }

        function format_clock(total_minutes) {
            total_minutes = ((total_minutes % 1440) + 1440) % 1440;

            var hours = Math.floor(total_minutes / 60);
            var minutes = total_minutes % 60;

            return (hours < 10 ? '0' : '') + hours
                + ':'
                + (minutes < 10 ? '0' : '') + minutes;
        }

        // Heartbeat: an open/close blocks the sketch's loop for the
        // duration of the motor move. A delayed /ping is therefore the
        // client-side signal that a move is currently under way.
        (function() {
            var warn = document.getElementById('move_warn');
            var in_flight = false;

            function ping() {
                if (in_flight) {
                    return;
                }

                in_flight = true;

                var slow = false;
                var timer = setTimeout(function() {
                    slow = true;
                    warn.style.display = 'block';
                }, 1500);

                fetch('/ping')
                    .then(function() {
                        clearTimeout(timer);
                        in_flight = false;

                        if (slow) {
                            warn.style.display = 'none';
                        }
                    })
                    .catch(function() {
                        clearTimeout(timer);
                        in_flight = false;
                    });
            }

            setInterval(ping, 3000);
        })();
    </script>
</body>
</html>
)HTML";
