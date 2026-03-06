# UI Refactoring: Remove Interactive Tests Page & Simplify Overlays

## OBIETTIVO
Rimuovere la pagina "Interactive Tests" (terza tile del tileview), semplificare l'overlay delle periferiche hardware e creare un nuovo overlay dedicato per i debug tools con 2 bottoni funzionali.

---

## MODIFICHE RICHIESTE

### 1. ELIMINA PAGINA INTERACTIVE TESTS
**File**: `main/ui/ui_tasks.c`

- Rimuovi la tile "Interactive Tests" (terza pagina del tileview)
- Identifica e rimuovi tutte le card associate:
  - Pattern Test
  - Color Test  
  - Touch Test
  - Touchpad Test
  - Altri test interattivi presenti
- Rimuovi la funzione `show_test_overlay()` e tutti i relativi callback
- Aggiorna **page indicators** da 3 a 2 pallini
- Aggiorna la **logica swipe** per gestire solo 2 pagine (Hardware Peripherals + Debug & Development Tools)

---

### 2. SEMPLIFICA PERIPHERAL OVERLAY
**File**: `main/ui/ui_tasks.c`  
**Funzione**: `show_peripheral_overlay()`

L'overlay che appare quando si clicca su una card della pagina "Hardware Peripherals" deve contenere **SOLO**:

- **Titolo peripheral** (es. "Display LCD", "Touch Controller", "SD Card")
- **Status badge** con colore dinamico:
  - Verde (`0x4CAF50`) per OK
  - Arancio (`0xFF9800`) per WARN
  - Rosso (`0xF44336`) per ERR
- **Description text** (2-3 righe di info)
- **GPIO Pin Configuration** (lista pin utilizzati con formato: `GPIO X (Function)`)  
  Esempio:
  ```
  GPIO 8 (I2C_SCL)
  GPIO 9 (I2C_SDA)
  GPIO 45 (RESET)
  ```
- **Back button** (chiude overlay e torna alla pagina principale)

**RIMUOVI**:
- ❌ Bottone "Run Test"
- ❌ Bottone "View Logs"  
- ❌ Bottone "Configure"
- ❌ Sezione configurazione dettagliata (le info sono già visibili nella pagina Status)

#### Esempio layout peripheral overlay:
```c
// Titolo
lv_label_set_text(title_label, "Display LCD");

// Status badge (verde/arancio/rosso)
lv_obj_set_style_bg_color(status_badge, lv_color_hex(0x4CAF50), 0);
lv_label_set_text(status_label, "OK");

// Description
lv_label_set_text(desc_label, 
    "MIPI DSI Display\n"
    "800x480 pixels\n"
    "Running at 60fps"
);

// GPIO Pins
lv_label_set_text(pins_label,
    "Pins:\n"
    "GPIO 8 (I2C_SCL)\n"
    "GPIO 9 (I2C_SDA)\n"
    "GPIO 45 (RESET)\n"
    "GPIO 47 (BACKLIGHT)"
);
```

---

### 3. CREA DEBUG TOOL OVERLAY
**File**: `main/ui/ui_tasks.c`  
**Nuova funzione**: `show_debug_tool_overlay(const char *tool_name)`

Nuovo overlay che appare quando si clicca su una card della pagina "Debug & Development Tools":

#### Contenuto overlay:
- **Titolo tool** (es. "Log Monitor", "Camera Test", "I2C Scanner")
- **Description text** (breve descrizione funzionalità tool)
- **2 BOTTONI FUNZIONALI**:
  1. **"Run Tool"** → callback `on_debug_tool_run_clicked(lv_event_t *e)`
     - Colore verde: `lv_obj_set_style_bg_color(btn_run, lv_color_hex(0x4CAF50), 0);`
  2. **"View Logs"** → callback `on_debug_tool_logs_clicked(lv_event_t *e)`  
     - Colore blu: `lv_obj_set_style_bg_color(btn_logs, lv_color_hex(0x2196F3), 0);`
- **Back button** (chiude overlay)

#### Layout bottoni:
```c
// Bottone "Run Tool" (verde)
lv_obj_t *btn_run = lv_btn_create(overlay_cont);
lv_obj_set_size(btn_run, 200, 50);
lv_obj_set_style_bg_color(btn_run, lv_color_hex(0x4CAF50), 0);
lv_obj_add_event_cb(btn_run, on_debug_tool_run_clicked, LV_EVENT_CLICKED, (void*)tool_name);

lv_obj_t *label_run = lv_label_create(btn_run);
lv_label_set_text(label_run, "Run Tool");
lv_obj_center(label_run);

// Bottone "View Logs" (blu)
lv_obj_t *btn_logs = lv_btn_create(overlay_cont);
lv_obj_set_size(btn_logs, 200, 50);
lv_obj_set_style_bg_color(btn_logs, lv_color_hex(0x2196F3), 0);
lv_obj_add_event_cb(btn_logs, on_debug_tool_logs_clicked, LV_EVENT_CLICKED, (void*)tool_name);

lv_obj_t *label_logs = lv_label_create(btn_logs);
lv_label_set_text(label_logs, "View Logs");
lv_obj_center(label_logs);
```

---

### 4. CALLBACK PLACEHOLDERS
**File**: `main/ui/ui_tasks.c`

Aggiungi le seguenti funzioni callback:

```c
static void on_debug_tool_run_clicked(lv_event_t *e) {
    const char *tool_name = (const char*)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "User clicked 'Run Tool' for: %s", tool_name);
    
    // TODO: Implementare chiamata a main.c per eseguire il tool
    // Es: ui_send_event(UI_EVENT_RUN_DEBUG_TOOL, tool_name);
}

static void on_debug_tool_logs_clicked(lv_event_t *e) {
    const char *tool_name = (const char*)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "User clicked 'View Logs' for: %s", tool_name);
    
    // TODO: Implementare schermata dedicata per visualizzare i log del tool specifico
    // Es: ui_show_tool_logs_screen(tool_name);
}
```

---

### 5. COLLEGA DEBUG TOOLS AL NUOVO OVERLAY
**File**: `main/ui/ui_tasks.c`  
**Funzione**: `on_debug_card_clicked(lv_event_t *e)`

Modifica il callback esistente per chiamare `show_debug_tool_overlay()` invece di `show_peripheral_overlay()`:

```c
static void on_debug_card_clicked(lv_event_t *e) {
    const char *tool_name = (const char*)lv_event_get_user_data(e);
    ESP_LOGI(TAG, "Debug tool card clicked: %s", tool_name);
    
    // Chiama il nuovo overlay specifico per debug tools
    show_debug_tool_overlay(tool_name);
}
```

---

## TESTING CHECKLIST

- [ ] Swipe funziona correttamente tra 2 pagine (non più 3)
- [ ] Page indicators mostrano 2 pallini (attivo/inattivo)
- [ ] Click su peripheral card (pagina 1) → overlay semplice **senza bottoni azione**
- [ ] Peripheral overlay mostra **GPIO pins** con formato `GPIO X (Function)`
- [ ] Click su debug tool card (pagina 2) → overlay con **2 bottoni funzionali**
- [ ] Bottone "Run Tool" stampa log `ESP_LOGI` con nome tool
- [ ] Bottone "View Logs" stampa log `ESP_LOGI` con nome tool
- [ ] Bottone "Back" chiude overlay e torna alla pagina principale
- [ ] Nessun crash o memory leak

---

## FILE DA MODIFICARE

- `main/ui/ui_tasks.c` (modifiche principali)
- `main/ui/ui_tasks.h` (se necessario aggiungere dichiarazioni funzioni)

---

## NOTE IMPLEMENTATIVE

- **Riutilizza lo stile** degli overlay esistenti per mantenere coerenza visiva
- **Memory management**: assicurati che `tool_name` rimanga valido per tutta la vita del callback (usa stringhe statiche o alloca memoria)
- **GPIO Pins**: usa font monospace per allineare i pin nella lista (es. `lv_obj_set_style_text_font(pins_label, &lv_font_montserrat_14, 0);`)
- **Estendibilità**: la struttura dei 2 bottoni permette future integrazioni (es. comunicazione con main.c via eventi)
- **Logs**: usa sempre `ESP_LOGI(TAG, ...)` per debugging, mai `printf()`
