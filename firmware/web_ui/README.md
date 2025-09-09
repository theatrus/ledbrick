# LEDBrick Web UI

This is a standalone web interface for controlling LEDBrick devices without Home Assistant.

## Features

- **Direct HTTP Control** - Uses ESPHome's REST API
- **Schedule Management** - Add/remove schedule points
- **Preset Loading** - Quick access to built-in presets
- **JSON Import/Export** - Full schedule backup and restore
- **Real-time Status** - Monitor scheduler state and current values
- **PWM Control** - Global brightness adjustment
- **No Installation Required** - Runs entirely in the browser

## Usage

### Option 1: Host Locally
1. Open `index.html` in a web browser
2. Enter your LEDBrick device URL (e.g., `http://ledbrickplus.local` or `http://192.168.1.100`)
3. Enter username/password if authentication is enabled
4. Click "Test Connection"

### Option 2: Host on GitHub Pages
1. Fork this repository
2. Enable GitHub Pages in repository settings
3. Access via `https://[username].github.io/[repo]/firmware/web_ui/`

### Option 3: Embed in ESPHome
Add to your ESPHome configuration:
```yaml
web_server:
  port: 80
  js_include: "web_ui/ledbrick.js"
  css_include: "web_ui/style.css"
```

## API Endpoints Used

The web UI uses ESPHome's REST API:

- `GET /switch/web_scheduler_enable` - Get scheduler state
- `POST /switch/web_scheduler_enable/turn_on` - Enable scheduler
- `POST /switch/web_scheduler_enable/turn_off` - Disable scheduler
- `POST /number/pwm_scale/set?value=X` - Set global brightness
- `POST /select/preset_selector/set?option=X` - Load preset
- `POST /button/[name]/press` - Trigger button actions
- `GET /text_sensor/[name]` - Get text sensor values
- `POST /text/[name]/set?value=X` - Set text input values

## Limitations

1. **JSON Size** - Large schedules may exceed URL parameter limits
2. **Authentication** - Only Basic Auth is supported
3. **CORS** - May need to disable CORS in browser for local files
4. **Memory** - Web server uses significant ESP memory

## CORS Issues

If you encounter CORS errors when accessing from `file://`:

### Chrome
```bash
chrome --disable-web-security --user-data-dir=/tmp/chrome
```

### Firefox
1. Navigate to `about:config`
2. Set `security.fileuri.strict_origin_policy` to `false`

### Safari
1. Enable Developer menu
2. Disable "Disable Cross-Origin Restrictions"

## Development

The web UI is built with vanilla JavaScript for maximum compatibility.

### Adding New Features

1. Add template components in `web_server.yaml`
2. Add UI controls in `index.html`
3. Add API calls in `ledbrick.js`

### Testing

1. Enable web server in ESPHome configuration
2. Upload firmware to device
3. Open web UI and test all functions
4. Check browser console for errors

## Security Considerations

- Always use authentication in production
- Consider using HTTPS proxy (nginx, Caddy)
- Limit network access to trusted devices
- Regularly update ESPHome firmware

## Troubleshooting

### Connection Failed
- Check device URL is correct
- Verify device is on same network
- Try IP address instead of hostname
- Check firewall settings

### Authentication Issues
- Verify username/password are correct
- Try without authentication first
- Check ESPHome logs for auth errors

### JSON Import Fails
- Validate JSON format
- Check for size limits
- Try smaller schedules
- Use minified JSON

### Memory Issues
- Disable unused features
- Use ESP32 instead of ESP8266
- Reduce web server features
- Monitor free heap