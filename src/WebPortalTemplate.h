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

            <p>
                Local time: <strong>{{LOCAL_TIME}}</strong>
            </p>

            <label>
                UTC offset
                <input
                    type='number'
                    step='1'
                    name='utc_offset'
                    value='{{UTC_OFFSET}}'>
            </label>

            <p>UTC time: {{UTC_TIME}}</p>
        </fieldset>

        <fieldset>
            <legend>Sun ephemeris</legend>

            <label>
                Latitude (-90..90)
                <input
                    type='number'
                    step='0.0001'
                    name='latitude'
                    value='{{LATITUDE}}'>
            </label>

            <label>
                Longitude (-180..180)
                <input
                    type='number'
                    step='0.0001'
                    name='longitude'
                    value='{{LONGITUDE}}'>
            </label>

            <p>
                Next sunrise:
                <span id='sunrise_local'>{{SUNRISE_LOCAL}}</span>
                (<span id='sunrise_utc'>{{SUNRISE_UTC}}</span> UTC)
            </p>

            <p>
                Next sunset:
                <span id='sunset_local'>{{SUNSET_LOCAL}}</span>
                (<span id='sunset_utc'>{{SUNSET_UTC}}</span> UTC)
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
                <input
                    type='radio'
                    name='open_mode'
                    value='timeofday'
                    {{OPEN_TIMEOFDAY_CHECKED}}>
                At a fixed time
            </label>

            <input
                type='time'
                name='open_timeofday_local'
                value='{{OPEN_TIMEOFDAY_LOCAL}}'>

            <label>
                <input
                    type='radio'
                    name='open_mode'
                    value='sun'
                    {{OPEN_SUN_CHECKED}}>
                Relative to sunrise (minutes offset, +/-)
            </label>

            <input
                type='number'
                min='-720'
                max='720'
                step='1'
                name='open_sun_offset'
                value='{{OPEN_SUN_OFFSET}}'>

            <p id='open_preview' class='preview'></p>
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
                <input
                    type='radio'
                    name='close_mode'
                    value='timeofday'
                    {{CLOSE_TIMEOFDAY_CHECKED}}>
                At a fixed time
            </label>

            <input
                type='time'
                name='close_timeofday_local'
                value='{{CLOSE_TIMEOFDAY_LOCAL}}'>

            <label>
                <input
                    type='radio'
                    name='close_mode'
                    value='sun'
                    {{CLOSE_SUN_CHECKED}}>
                Relative to sunset (minutes offset, +/-)
            </label>

            <input
                type='number'
                min='-720'
                max='720'
                step='1'
                name='close_sun_offset'
                value='{{CLOSE_SUN_OFFSET}}'>

            <p id='close_preview' class='preview'></p>
        </fieldset>

        <fieldset>
            <legend>Motor</legend>

            <label>
                Open duration, ms
                <input
                    type='number'
                    name='motor_open_duration_ms'
                    value='{{MOTOR_OPEN_DURATION_MS}}'>
            </label>

            <label>
                Close duration, ms
                <input
                    type='number'
                    name='motor_close_duration_ms'
                    value='{{MOTOR_CLOSE_DURATION_MS}}'>
            </label>

            <label>
                <input
                    type='checkbox'
                    name='motor_invert_direction'
                    {{MOTOR_INVERT_DIRECTION_CHECKED}}>
                Invert direction
            </label>
        </fieldset>

        <button type='submit'>Save settings</button>
    </form>

    <fieldset class='force'>
        <legend>Debug</legend>

        <form method='POST' action='/force-open' style='display:inline'>
            <button type='submit'>Force Open</button>
        </form>

        <form method='POST' action='/force-close' style='display:inline'>
            <button type='submit'>Force Close</button>
        </form>

        <form method='POST' action='/sleep' style='display:inline'>
            <button type='submit'>Sleep now</button>
        </form>

        <form method='POST' action='/nap' style='display:inline'>
            <button type='submit'>Nap and open</button>
        </form>

        <p>Last event: {{LAST_EVENT}}</p>
    </fieldset>

    {{DEBUG_LOG_SECTION}}

    <script>
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
            var match = value.trim().match(/^(\d{1,2}):(\d{2})$/);

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

        function render_local_utc_preview(
            local_value,
            local_reference,
            utc_reference
        ) {
            if (
                local_value === null
                || local_reference === null
                || utc_reference === null
            ) {
                return null;
            }

            var utc_value =
                utc_reference + (local_value - local_reference);

            return format_clock(local_value)
                + ' local ('
                + format_clock(utc_value)
                + ' UTC)';
        }

        function update_preview(
            mode_name,
            timeofday_name,
            offset_name,
            event_local_id,
            event_utc_id,
            preview_id
        ) {
            var mode = document.querySelector(
                "input[name='" + mode_name + "']:checked"
            );
            var preview = document.getElementById(preview_id);

            if (!mode) {
                return;
            }

            var local_event = parse_clock(
                document.getElementById(event_local_id).textContent
            );
            var utc_event = parse_clock(
                document.getElementById(event_utc_id).textContent
            );

            if (local_event === null || utc_event === null) {
                preview.textContent =
                    'Preview unavailable: sync time first.';
                return;
            }

            if (mode.value === 'timeofday') {
                var timeofday = document.querySelector(
                    "input[name='" + timeofday_name + "']"
                ).value;

                var timeofday_local = parse_clock(timeofday);

                if (timeofday_local === null) {
                    preview.textContent =
                        'Preview unavailable: enter a time.';
                    return;
                }

                preview.textContent =
                    'Preview: '
                    + render_local_utc_preview(
                        timeofday_local,
                        local_event,
                        utc_event
                    );

                return;
            }

            var offset_text = document.querySelector(
                "input[name='" + offset_name + "']"
            ).value.trim();

            var offset = offset_text === ''
                ? NaN
                : Number(offset_text);

            if (!Number.isInteger(offset)) {
                preview.textContent =
                    'Preview unavailable: enter a whole-minute offset.';
                return;
            }

            preview.textContent =
                'Preview: '
                + render_local_utc_preview(
                    local_event + offset,
                    local_event,
                    utc_event
                );
        }

        function update_door_previews() {
            update_preview(
                'open_mode',
                'open_timeofday_local',
                'open_sun_offset',
                'sunrise_local',
                'sunrise_utc',
                'open_preview'
            );

            update_preview(
                'close_mode',
                'close_timeofday_local',
                'close_sun_offset',
                'sunset_local',
                'sunset_utc',
                'close_preview'
            );
        }

        document.querySelectorAll(
            "input[name='open_mode'],"
            + " input[name='open_timeofday_local'],"
            + " input[name='open_sun_offset'],"
            + " input[name='close_mode'],"
            + " input[name='close_timeofday_local'],"
            + " input[name='close_sun_offset']"
        ).forEach(function(input) {
            input.addEventListener('input', update_door_previews);
            input.addEventListener('change', update_door_previews);
        });

        update_door_previews();

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
