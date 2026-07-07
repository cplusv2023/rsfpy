#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#include <gio/gio.h>
#include <glib.h>

/*
 * Use raw pasteboard type strings instead of NSPasteboardType* constants.
 * This keeps the small Objective-C bridge friendly to older SDKs and avoids
 * availability/link surprises when the project is built on different macOS
 * versions.  The types below are stable UTIs or long-standing pasteboard names.
 */
#define RSFPY_PB_SVG_UTI       @"public.svg-image"
#define RSFPY_PB_SVG_UTI_ALT   @"public.svg"
#define RSFPY_PB_SVG_MIME      @"image/svg+xml"
#define RSFPY_PB_SVG_ADOBE     @"com.adobe.svg"
#define RSFPY_PB_SVG_W3C       @"org.w3c.svg"
#define RSFPY_PB_PNG_UTI       @"public.png"
#define RSFPY_PB_PNG_MIME      @"image/png"
#define RSFPY_PB_FILE_URL      @"public.file-url"
#define RSFPY_PB_FILE_URL_OLD  @"NSURLPboardType"
#define RSFPY_PB_FILENAMES_OLD @"NSFilenamesPboardType"
#define RSFPY_PB_TEXT_UTF8     @"public.utf8-plain-text"
#define RSFPY_PB_TEXT_LEGACY   @"NSStringPboardType"

gboolean rsfpy_macos_copy_svg_text_to_clipboard(const gchar *svg,
                                                gsize svg_len,
                                                const guint8 *png,
                                                gsize png_len,
                                                GError **err)
{
    @autoreleasepool {
        NSData *svgData;
        NSData *pngData;
        NSString *svgString;
        NSString *svgPath;
        NSURL *svgURL;
        NSPasteboard *pasteboard;
        NSMutableArray *types;
        gboolean includeSvgFiles;
        gboolean includeSvgText;
        BOOL wroteTempSvg;
        BOOL wroteSvgType;
        BOOL wroteSvgAltType;
        BOOL wroteMimeType;
        BOOL wroteAdobeType;
        BOOL wroteW3cType;
        BOOL wrotePngType;
        BOOL wrotePngMimeType;
        BOOL wroteFileUrl;
        BOOL wroteOldFileUrl;
        BOOL wroteOldFilenames;
        BOOL wroteText;
        BOOL wroteLegacyText;

        if (!svg && (!png || png_len == 0)) {
            g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "no clipboard data to copy");
            return FALSE;
        }

        includeSvgFiles = g_getenv("RSFPY_CLIPBOARD_INCLUDE_SVG_FILE") &&
            !g_str_equal(g_getenv("RSFPY_CLIPBOARD_INCLUDE_SVG_FILE"), "0");
        includeSvgText = g_getenv("RSFPY_CLIPBOARD_INCLUDE_SVG_TEXT") &&
            !g_str_equal(g_getenv("RSFPY_CLIPBOARD_INCLUDE_SVG_TEXT"), "0");

        svgData = svg ? [NSData dataWithBytes:svg length:(NSUInteger)svg_len] : nil;
        pngData = (png && png_len > 0)
            ? [NSData dataWithBytes:png length:(NSUInteger)png_len]
            : nil;
        svgString = (svg && includeSvgText) ? [NSString stringWithUTF8String:svg] : nil;
        if (svg && !svgString) {
            if (includeSvgText) {
                svgString = [NSString stringWithFormat:@"%.*s", (int)svg_len, svg];
            }
        }
        if (svg && !svgData) {
            g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "failed to prepare SVG clipboard data");
            return FALSE;
        }
        if (png && !pngData) {
            g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "failed to prepare PNG clipboard data");
            return FALSE;
        }

        svgPath = (svgData && includeSvgFiles) ? [NSTemporaryDirectory() stringByAppendingPathComponent:
            [NSString stringWithFormat:@"rsfpy-clipboard-%@.svg",
             [[NSUUID UUID] UUIDString]]] : nil;
        svgURL = svgPath ? [NSURL fileURLWithPath:svgPath] : nil;
        wroteTempSvg = svgPath ? [svgData writeToFile:svgPath atomically:YES] : NO;
        if (!wroteTempSvg) {
            svgURL = nil;
            svgPath = nil;
        }

        pasteboard = [NSPasteboard generalPasteboard];
        if (!pasteboard) {
            g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "macOS pasteboard is not available");
            return FALSE;
        }

        types = [NSMutableArray array];
        if (pngData) {
            [types addObject:RSFPY_PB_PNG_UTI];
            [types addObject:RSFPY_PB_PNG_MIME];
        }
        if (svgData) {
            [types addObject:RSFPY_PB_SVG_UTI];
            [types addObject:RSFPY_PB_SVG_UTI_ALT];
            [types addObject:RSFPY_PB_SVG_MIME];
            [types addObject:RSFPY_PB_SVG_ADOBE];
            [types addObject:RSFPY_PB_SVG_W3C];
        }
        if (svgString) {
            [types addObject:RSFPY_PB_TEXT_UTF8];
            [types addObject:RSFPY_PB_TEXT_LEGACY];
        }
        if (svgURL && svgPath) {
            [types addObject:RSFPY_PB_FILE_URL];
            [types addObject:RSFPY_PB_FILE_URL_OLD];
            [types addObject:RSFPY_PB_FILENAMES_OLD];
        }

        [pasteboard declareTypes:types owner:nil];

        wroteSvgType = svgData ? [pasteboard setData:svgData forType:RSFPY_PB_SVG_UTI] : NO;
        wroteSvgAltType = svgData ? [pasteboard setData:svgData forType:RSFPY_PB_SVG_UTI_ALT] : NO;
        wroteMimeType = svgData ? [pasteboard setData:svgData forType:RSFPY_PB_SVG_MIME] : NO;
        wroteAdobeType = svgData ? [pasteboard setData:svgData forType:RSFPY_PB_SVG_ADOBE] : NO;
        wroteW3cType = svgData ? [pasteboard setData:svgData forType:RSFPY_PB_SVG_W3C] : NO;
        wrotePngType = pngData ? [pasteboard setData:pngData forType:RSFPY_PB_PNG_UTI] : NO;
        wrotePngMimeType = pngData ? [pasteboard setData:pngData forType:RSFPY_PB_PNG_MIME] : NO;
        wroteFileUrl = svgURL ? [pasteboard setString:[svgURL absoluteString]
                                           forType:RSFPY_PB_FILE_URL] : NO;
        wroteOldFileUrl = svgURL ? [pasteboard setString:[svgURL absoluteString]
                                              forType:RSFPY_PB_FILE_URL_OLD] : NO;
        wroteOldFilenames = svgPath ? [pasteboard setPropertyList:@[svgPath]
                                                 forType:RSFPY_PB_FILENAMES_OLD] : NO;
        wroteText = svgString ? [pasteboard setString:svgString forType:RSFPY_PB_TEXT_UTF8] : NO;
        wroteLegacyText = svgString ? [pasteboard setString:svgString forType:RSFPY_PB_TEXT_LEGACY] : NO;

        if (!wroteSvgType && !wroteSvgAltType &&
            !wroteMimeType && !wroteAdobeType && !wroteW3cType &&
            !wrotePngType && !wrotePngMimeType &&
            !wroteFileUrl && !wroteOldFileUrl && !wroteOldFilenames &&
            !wroteText && !wroteLegacyText) {
            g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "failed to set macOS pasteboard data");
            return FALSE;
        }

        return TRUE;
    }
}
