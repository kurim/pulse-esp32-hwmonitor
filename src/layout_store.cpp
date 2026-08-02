#include "layout_store.h"
#include <Preferences.h>

static const char *NS = "pulselayout";

bool layout_store_save(const String &json) {
    if (json.length() > kMaxLayoutJsonBytes) {
        return false;
    }
    Preferences p;
    if (!p.begin(NS, /*readOnly=*/false)) {
        return false;
    }
    p.putString("layout", json);
    p.end();
    return true;
}

String layout_store_load(void) {
    Preferences p;
    if (!p.begin(NS, /*readOnly=*/true)) {
        return "";
    }
    String s = p.getString("layout", "");
    p.end();
    return s;
}

void layout_store_clear(void) {
    Preferences p;
    if (p.begin(NS, /*readOnly=*/false)) {
        p.clear();
        p.end();
    }
}
