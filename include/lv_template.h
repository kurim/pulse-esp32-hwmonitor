#include <lvgl.h>

// static: dieser Header definiert (nicht nur deklariert) style_card und die
// beiden Funktionen darunter - ohne static gaebe es "multiple definition"-
// Linkerfehler, sobald mehr als eine .cpp-Datei ihn included (aktuell
// display_draw.cpp UND display_layout.cpp fuer die Dashboard-Editor-Cards).
static lv_style_t style_card;
static bool style_inited = false;

static void init_card_style(void) {
    if (style_inited) return;

    lv_style_init(&style_card);
    
    // Hintergrund & Gradient (Dark Theme)
    lv_style_set_bg_color(&style_card, lv_color_hex(0x1E293B));

    // Ecken & Abstände - beides klein gehalten (User-Vorgabe "radius auf 5px",
    // "Innenabstand kleiner, damit der Platz effizienter genutzt wird"),
    // gerade auf dem CYD (320x240) ist der Platz pro Karte knapp.
    lv_style_set_radius(&style_card, 5);
    lv_style_set_pad_all(&style_card, 5);
    
    // NEU: Moderner, leicht leuchtender Rand (wie auf deinem Foto!)
    lv_style_set_border_color(&style_card, lv_color_hex(0x38BDF8)); // Neon Blue / Cyan
    lv_style_set_border_width(&style_card, 2);
    lv_style_set_border_opa(&style_card, LV_OPA_40); // Leicht transparent

    style_inited = true;
}

typedef struct {
    lv_obj_t * card;
} card_container_t;

static card_container_t create_card(lv_obj_t * parent, int sizeX, int sizeY) {
    init_card_style();

    card_container_t comp;

    // Haupt-Container
    comp.card = lv_obj_create(parent);
    lv_obj_add_style(comp.card, &style_card, 0);
    lv_obj_set_size(comp.card, sizeX, sizeY);
    
    // Wichtig: Scrollbalken & Verhalten deaktivieren
    lv_obj_remove_flag(comp.card, LV_OBJ_FLAG_SCROLLABLE);

    // Vertikales Flexbox-Layout für Dashboards
    lv_obj_set_layout(comp.card, LV_LAYOUT_FLEX);
    lv_obj_set_flex_flow(comp.card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(comp.card, 
                          LV_FLEX_ALIGN_SPACE_BETWEEN, // Gleichmäßig von oben nach unten verteilen
                          LV_FLEX_ALIGN_CENTER,        // Horizontal zentrieren
                          LV_FLEX_ALIGN_CENTER);

    return comp;
}
