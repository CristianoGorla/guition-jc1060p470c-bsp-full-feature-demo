# Contributing Guidelines

## Documentation Rules

### Automatic Troubleshooting Updates

**Rule: When a problem is solved, document the solution immediately.**

When you encounter and solve an initialization or hardware issue:

1. **Capture the Boot Log**
   - Copy the complete boot output showing the **successful initialization**
   - Include timestamps and all relevant device messages
   - Ensure the log shows the problem is resolved

2. **Update `troubleshooting.md`**
   - Add a new section describing:
     - **Problem**: Original error messages or symptoms
     - **Root Cause**: Technical explanation of why it failed
     - **Solution**: Code changes or configuration that fixed it
     - **Why This Works**: Explanation of the fix
     - **Successful Output**: Complete boot log showing working state
   
3. **Keep Historical Context**
   - Preserve existing content in `troubleshooting.md`
   - Add new sections, don't replace
   - Include "Deprecated Approaches" if old methods were removed
   - Document the evolution of the solution

4. **Consistent Pattern Documentation**
   - If a solution applies to multiple devices (e.g., GT911 and RTC), document the pattern
   - Create a "Complete Initialization Sequence" section
   - Show before/after comparisons

### Example Structure

```markdown
## Device Name Issue

### Problem: [Descriptive Title]

**Symptoms:**
```
[Error messages from boot log]
```

**Root Cause:**
[Technical explanation]

**Solution:**
[Code changes]

**Why This Works:**
1. [Point 1]
2. [Point 2]
3. [Point 3]

**Successful Output:**
```
[Working boot log]
```
```

### What to Document

✅ **DO Document:**
- Hardware initialization failures and fixes
- I2C device detection issues
- Timing-sensitive operations (reset sequences)
- Configuration patterns that solve multiple issues
- Boot logs showing successful operation
- Feature flag configurations that work

❌ **DON'T Document:**
- Temporary debugging attempts
- Incomplete solutions
- Platform-specific workarounds without explanation

### When to Update

**Immediately after:**
- Fixing a device initialization error
- Solving an I2C communication problem
- Establishing a working configuration
- Finding a pattern that applies across devices

**Before moving to next task:**
- Ensure `troubleshooting.md` is updated
- Verify boot log is captured
- Confirm feature flags are documented

---

## Code Style

### Feature Flags

- Use `feature_flags.h` for all optional features
- Document why each flag exists
- Use consistent naming: `ENABLE_*`, `DEBUG_*`
- Group related flags together

### Initialization Sequence

- Initialize devices in order of dependencies
- No I2C scan before device init (prevents wake-up)
- Let drivers validate device presence internally
- Document the complete boot sequence

### Logging

- Use appropriate log levels:
  - `ESP_LOGI`: Normal operation milestones
  - `ESP_LOGW`: Non-critical issues
  - `ESP_LOGE`: Critical failures
- Include context in messages
- Use ✓/✗ symbols for visual feedback

---

## Git Commit Messages

**Format:**
```
[Component] Short description (50 chars max)

- Detailed point 1
- Detailed point 2
- Reference to issue/pattern
```

**Examples:**
```
RTC init: apply GT911 pattern (direct init, no probe)

- Remove i2c_master_probe before RTC init
- RTC driver handles detection internally via first read
- Same philosophy as GT911: let driver validate device presence
```

```
Disable I2C scan before touch init - prevents GT911 wake-up conflict

- Set ENABLE_I2C_SCAN to 0
- I2C scan interferes with GT911 hardware reset sequence
- Driver needs clean state for address detection
```

---

## Testing Workflow

1. **Make minimal changes**
2. **Test immediately** (`idf.py build flash monitor`)
3. **Capture boot log** (successful or failed)
4. **Document if successful**
5. **Repeat**

---

## Questions?

This project uses:
- ESP-IDF v5.5.3
- ESP32-P4 chip (360MHz dual-core)
- Guition JC1060P470C board
- Multiple I2C devices on shared bus

See `troubleshooting.md` for hardware-specific issues and solutions.
