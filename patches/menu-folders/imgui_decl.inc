#include <cstring>

/* Shadowgreat patch (patches/menu-folders): from the patched
 * recomp_runtime_ui.c. */
extern "C" int recomp_runtime_ui_sg_folder_open(const RecompRuntimeUi *ui, size_t row);
extern "C" int recomp_runtime_ui_sg_row_nested(const RecompRuntimeUi *ui, size_t row);
