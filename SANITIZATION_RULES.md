# Sanitization Rules - Permanent Workflow

**Last Updated:** 2026-03-01  
**Version:** 1.0.0-beta

## Purpose

This document defines **permanent sanitization rules** for all text files in this repository. These rules are enforced automatically via GitHub Actions and must be followed for all future updates.

## Automated Enforcement

**GitHub Actions Workflow:** `.github/workflows/sanitize_check.yml`

- Runs on every push and pull request
- Scans `.md`, `.txt`, `.log`, `.c`, `.h`, `.yml` files
- Fails CI if sensitive data is detected
- Provides actionable error messages

## Sanitization Conversion Table

### IP Addresses

| Description | Real Value | Sanitized Value |
|-------------|------------|----------------|
| Gateway/Router | 192.168.x.1 | **10.0.0.1** |
| ESP32-P4 Host | 192.168.x.100 | **10.0.0.100** |
| ESP32-C6 WiFi | 192.168.x.101 | **10.0.0.101** |
| Other Devices | 192.168.x.x | **10.0.0.x** |

**Rule:** Replace ALL private IP addresses (192.168.x.x, 172.16-31.x.x, 10.x.x.x) with sanitized equivalents from 10.0.0.0/24 range.

### MAC Addresses

| Description | Sanitized Value |
|-------------|----------------|
| Gateway/Router | **AA:BB:CC:00:00:01** |
| ESP32-P4 Host | **AA:BB:CC:00:00:02** |
| ESP32-C6 WiFi | **AA:BB:CC:00:00:03** |

**Rule:** Replace real MAC addresses with the AA:BB:CC:00:00:xx pattern.

### WiFi SSIDs

| Context | Sanitized SSID |
|---------|---------------|
| Production/Beta | **GUITION_BETA_AP** |
| Guest Network | **GUEST_AP_X** |
| Example/Placeholder | **YourWiFiSSID** |

**Rule:** Replace real WiFi network names with generic placeholders.

**Exception:** In `main/wifi_config.h.example`, use placeholder `YourWiFiSSID` to guide users.

### File System Paths

| Platform | Real Path | Sanitized Path |
|----------|-----------|---------------|
| macOS | `/Users/username/.espressif/` | `${IDF_PATH}` or `$ENV{IDF_PATH}` |
| Linux | `/home/username/.espressif/` | `${IDF_PATH}` or `$ENV{IDF_PATH}` |
| Windows | `C:\Users\username\.espressif\` | `%IDF_PATH%` or `${IDF_PATH}` |
| Project | `/path/to/project/` | `${PROJECT_DIR}` |

**Rule:** Replace absolute paths with environment variables.

## Allowed Values (CI Whitelist)

### Allowed IP Addresses
```
10.0.0.1        # Gateway (sanitized)
10.0.0.100      # Host (sanitized)
10.0.0.101      # WiFi module (sanitized)
127.0.0.1       # Localhost (always allowed)
0.0.0.0         # Any address (always allowed)
255.255.255.0   # Netmask (always allowed)
```

### Allowed MAC Addresses
```
AA:BB:CC:00:00:01   # Gateway (sanitized)
AA:BB:CC:00:00:02   # Host (sanitized)
AA:BB:CC:00:00:03   # WiFi module (sanitized)
```

### Allowed SSIDs
```
GUITION_BETA_AP     # Production sanitized SSID
GUEST_AP_X          # Guest network placeholder
YourWiFiSSID        # Example placeholder
YourSSID            # Alternative placeholder
FRITZ!Box 7530 WL   # Example router (from screenshots)
```

### Allowed Path Variables
```
${IDF_PATH}         # ESP-IDF installation
$ENV{IDF_PATH}      # ESP-IDF environment variable
${PROJECT_DIR}      # Project root directory
%IDF_PATH%          # Windows environment variable
```

## Workflow for Future Updates

### When Adding New Documentation

1. **Write content** with real data (for local testing)
2. **Before commit**, apply sanitization rules:
   - Replace IPs: `192.168.x.x` → `10.0.0.x`
   - Replace MACs: real → `AA:BB:CC:00:00:0x`
   - Replace SSIDs: real → `GUITION_BETA_AP` or placeholder
   - Replace paths: absolute → environment variables
3. **Commit** sanitized version
4. **CI check** will validate sanitization

### When Adding Boot Logs

**Before adding logs to documentation:**
```bash
# Manual sanitization with sed/awk
sed -i 's/192\.168\.[0-9]\{1,3\}\.[0-9]\{1,3\}/10.0.0.100/g' bootlog.txt
sed -i 's/\/Users\/[a-zA-Z0-9_-]\+\//${PROJECT_DIR}\//g' bootlog.txt
```

**Or use the sanitization script (if created):**
```bash
./scripts/sanitize.sh bootlog.txt
```

### When Updating troubleshooting.md

**Agent Instruction (Permanent):**
> "Before saving any update to `troubleshooting.md` or other documentation files, automatically apply the sanitization conversion table defined in SANITIZATION_RULES.md. Replace all private IPs with 10.0.0.x, all MACs with AA:BB:CC:00:00:0x, all SSIDs with GUITION_BETA_AP or placeholders, and all absolute paths with environment variables."

## Files Excluded from Sanitization

### Gitignored Files (Never Committed)
```
main/wifi_config.h          # Real WiFi credentials (local only)
*.pdf                        # Confidential datasheets
*.log                        # Local log files
*.local                      # Local configuration
```

### Example/Template Files
```
main/wifi_config.h.example   # Uses placeholder SSIDs (allowed)
```

## Confidential Documents

**PDF datasheets and proprietary documentation are gitignored:**

```gitignore
# .gitignore entries
*.pdf
PDF/
docs/confidential/
datasheets/
```

**Policy:**
- PDFs can exist in local repo for reference
- Never committed to Git
- GitHub Actions verifies no PDFs in commits

## Manual Verification

Before pushing commits, verify sanitization locally:

```bash
# Check for private IPs
grep -r '192\.168\.' . --include="*.md" --include="*.txt"

# Check for local paths
grep -r '/Users/' . --include="*.md" --include="*.txt"
grep -r '/home/' . --include="*.md" --include="*.txt"

# Check for real MAC addresses (excluding sanitized)
grep -rE '([0-9A-Fa-f]{2}:){5}[0-9A-Fa-f]{2}' . --include="*.md" | grep -v 'AA:BB:CC'
```

## CI Failure Response

If the sanitization check fails:

1. **Read CI output** to identify file and line number
2. **Apply conversion table** to replace sensitive data
3. **Amend commit** with sanitized version:
   ```bash
   git add <file>
   git commit --amend --no-edit
   git push --force-with-lease
   ```
4. **CI will re-run** and should pass

## Version History

| Version | Date | Changes |
|---------|------|----------|
| 1.0.0-beta | 2026-03-01 | Initial sanitization rules, automated CI check |

## References

- **CI Workflow:** `.github/workflows/sanitize_check.yml`
- **Gitignore:** `.gitignore`
- **Version:** `VERSION`
- **Documentation:** `troubleshooting.md`, `README.md`

---

**Note:** These rules are **permanent and enforced automatically**. All future documentation updates must comply with this sanitization table.
