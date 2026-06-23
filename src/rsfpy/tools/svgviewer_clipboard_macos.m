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

        if (!svg) {
            g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "no SVG data to copy");
            return FALSE;
        }

        svgData = [NSData dataWithBytes:svg length:(NSUInteger)svg_len];
        pngData = (png && png_len > 0)
            ? [NSData dataWithBytes:png length:(NSUInteger)png_len]
            : nil;
        svgString = [NSString stringWithUTF8String:svg];
        if (!svgString) {
            svgString = [NSString stringWithFormat:@"%.*s", (int)svg_len, svg];
        }
        if (!svgData || !svgString) {
            g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "failed to prepare SVG clipboard data");
            return FALSE;
        }

        svgPath = [NSTemporaryDirectory() stringByAppendingPathComponent:
            [NSString stringWithFormat:@"rsfpy-clipboard-%@.svg",
             [[NSUUID UUID] UUIDString]]];
        svgURL = svgPath ? [NSURL fileURLWithPath:svgPath] : nil;
        if (svgPath) {
            [svgData writeToFile:svgPath atomically:YES];
        }

        pasteboard = [NSPasteboard generalPasteboard];
        if (!pasteboard) {
            g_set_error(err, G_IO_ERROR, G_IO_ERROR_FAILED,
                        "macOS pasteboard is not available");
            return FALSE;
        }

        [pasteboard declareTypes:@[
            RSFPY_PB_SVG_UTI,
            RSFPY_PB_SVG_UTI_ALT,
            RSFPY_PB_SVG_MIME,
            RSFPY_PB_SVG_ADOBE,
            RSFPY_PB_SVG_W3C,
            RSFPY_PB_PNG_UTI,
            RSFPY_PB_PNG_MIME,
            RSFPY_PB_FILE_URL,
            RSFPY_PB_FILE_URL_OLD,
            RSFPY_PB_FILENAMES_OLD,
            RSFPY_PB_TEXT_UTF8,
            RSFPY_PB_TEXT_LEGACY
        ] owner:nil];

        wroteSvgType = [pasteboard setData:svgData forType:RSFPY_PB_SVG_UTI];
        wroteSvgAltType = [pasteboard setData:svgData forType:RSFPY_PB_SVG_UTI_ALT];
        wroteMimeType = [pasteboard setData:svgData forType:RSFPY_PB_SVG_MIME];
        wroteAdobeType = [pasteboard setData:svgData forType:RSFPY_PB_SVG_ADOBE];
        wroteW3cType = [pasteboard setData:svgData forType:RSFPY_PB_SVG_W3C];
        wrotePngType = pngData ? [pasteboard setData:pngData forType:RSFPY_PB_PNG_UTI] : NO;
        wrotePngMimeType = pngData ? [pasteboard setData:pngData forType:RSFPY_PB_PNG_MIME] : NO;
        wroteFileUrl = svgURL ? [pasteboard setString:[svgURL absoluteString]
                                           forType:RSFPY_PB_FILE_URL] : NO;
        wroteOldFileUrl = svgURL ? [pasteboard setString:[svgURL absoluteString]
                                              forType:RSFPY_PB_FILE_URL_OLD] : NO;
        wroteOldFilenames = svgPath ? [pasteboard setPropertyList:@[svgPath]
                                                 forType:RSFPY_PB_FILENAMES_OLD] : NO;
        wroteText = [pasteboard setString:svgString forType:RSFPY_PB_TEXT_UTF8];
        wroteLegacyText = [pasteboard setString:svgString forType:RSFPY_PB_TEXT_LEGACY];

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
