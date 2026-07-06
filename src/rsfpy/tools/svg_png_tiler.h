#ifndef SVG_PNG_TILER_H
#define SVG_PNG_TILER_H

#include <glib.h>

G_BEGIN_DECLS

#define SVG_PNG_TILER_ERROR svg_png_tiler_error_quark()

typedef enum {
    SVG_PNG_TILER_ERROR_INVALID_ARGUMENT,
    SVG_PNG_TILER_ERROR_NOT_FOUND,
    SVG_PNG_TILER_ERROR_PARSE,
    SVG_PNG_TILER_ERROR_DATA_URI,
    SVG_PNG_TILER_ERROR_PNG,
    SVG_PNG_TILER_ERROR_CAIRO,
    SVG_PNG_TILER_ERROR_IO
} SvgPngTilerError;

GQuark svg_png_tiler_error_quark(void);

typedef struct SvgPngTilerOptions {
    /* Logical tile size in original PNG pixels. 512 is conservative for data URI size. */
    guint tile_px;

    /* Extra source pixels around every logical tile. 1 or 2 prevents SVG raster seams. */
    guint bleed_px;

    /* If non-zero, recursively split tiles until the whole data URI is <= this many chars,
       unless the logical tile has reached min_tile_px. */
    gsize max_data_uri_chars;

    /* If non-zero, rewrite images whose decoded dimensions exceed this many pixels. */
    gsize max_image_pixels;

    /* Smallest logical tile side used when enforcing max_data_uri_chars. */
    guint min_tile_px;

    /* Rewrite every embedded PNG image even if it is below the threshold. */
    gboolean force;

    /* Disable rewriting completely. */
    gboolean disable;

    /* Put the original id on the generated <g>. Tile ids become id__tile_R_C. */
    gboolean keep_original_id_on_group;

    /* Add style="image-rendering:inherit" to each generated tile image. */
    gboolean inherit_image_rendering;

    /* Emit a short XML comment before each replacement. */
    gboolean emit_comment;

    /* Use href even when the original used xlink:href. If FALSE, keep original href attr name. */
    gboolean force_href;
} SvgPngTilerOptions;

void svg_png_tiler_options_init(SvgPngTilerOptions *opt);
void svg_png_tiler_options_init_from_env(SvgPngTilerOptions *opt);

/* Rewrite every <image> whose href/xlink:href is data:image/png;base64,...
   Returned string must be freed with g_free(). */
gchar *svg_png_tiler_rewrite_all(const gchar *svg_utf8,
                                 const SvgPngTilerOptions *opt,
                                 GError **error);

gchar *svg_png_tiler_rewrite_all_full(const gchar *svg_utf8,
                                      const SvgPngTilerOptions *opt,
                                      gboolean *out_changed,
                                      GError **error);

/* Rewrite the embedded PNG <image> with matching id="...".
   Returned string must be freed with g_free(). */
gchar *svg_png_tiler_rewrite_one_by_id(const gchar *svg_utf8,
                                        const gchar *image_id,
                                        const SvgPngTilerOptions *opt,
                                        GError **error);

/* Convenience file wrapper. */
gboolean svg_png_tiler_rewrite_file(const gchar *input_svg_path,
                                    const gchar *output_svg_path,
                                    const gchar *image_id_or_null,
                                    const SvgPngTilerOptions *opt,
                                    GError **error);

G_END_DECLS

#endif /* SVG_PNG_TILER_H */
