// Copyright (c) Epic Games Tools
// Licensed under the MIT license (https://opensource.org/license/mit/)

////////////////////////////////
//~ Helpers

internal CGFontRef
fp_ct_cgfont_from_handle(FP_Handle handle)
{
  CGFontRef result = (CGFontRef)handle.u64[0];
  return result;
}

internal FP_Handle
fp_ct_handle_from_cgfont(CGFontRef font)
{
  FP_Handle result = {(U64)font};
  return result;
}

////////////////////////////////
//~ Backend Implementations

fp_hook void
fp_init(void)
{
  Arena *arena = arena_alloc();
  fp_ct_state = push_array(arena, FP_CT_State, 1);
  fp_ct_state->arena = arena;
}

fp_hook FP_Handle
fp_font_open(String8 path)
{
  Temp scratch = scratch_begin(0, 0);
  String8 path_copy = push_str8_copy(scratch.arena, path);
  CGFontRef font = 0;
  CGDataProviderRef provider = CGDataProviderCreateWithFilename((char *)path_copy.str);
  if(provider != 0)
  {
    font = CGFontCreateWithDataProvider(provider);
    CGDataProviderRelease(provider);
  }
  FP_Handle handle = fp_ct_handle_from_cgfont(font);
  scratch_end(scratch);
  return handle;
}

fp_hook FP_Handle
fp_font_open_from_static_data_string(String8 *data_ptr)
{
  CGFontRef font = 0;
  CGDataProviderRef provider = CGDataProviderCreateWithData(0, data_ptr->str, (size_t)data_ptr->size, 0);
  if(provider != 0)
  {
    font = CGFontCreateWithDataProvider(provider);
    CGDataProviderRelease(provider);
  }
  FP_Handle handle = fp_ct_handle_from_cgfont(font);
  return handle;
}

fp_hook void
fp_font_close(FP_Handle handle)
{
  CGFontRef font = fp_ct_cgfont_from_handle(handle);
  if(font != 0)
  {
    CGFontRelease(font);
  }
}

fp_hook FP_Metrics
fp_metrics_from_font(FP_Handle handle)
{
  CGFontRef font = fp_ct_cgfont_from_handle(handle);
  FP_Metrics result = {0};
  if(font != 0)
  {
    int ascent = CGFontGetAscent(font);
    int descent = CGFontGetDescent(font);
    int cap_height = CGFontGetCapHeight(font);
    result.design_units_per_em = (F32)CGFontGetUnitsPerEm(font);
    result.ascent              = (F32)ascent;
    result.descent             = (F32)(descent < 0 ? -descent : descent);
    result.line_gap            = (F32)CGFontGetLeading(font);
    result.capital_height      = (F32)(cap_height > 0 ? cap_height : ascent);
  }
  return result;
}

fp_hook FP_RasterResult
fp_raster(Arena *arena, FP_Handle handle, F32 size, FP_RasterFlags flags, String8 string)
{
  ProfBeginFunction();
  CGFontRef cgfont = fp_ct_cgfont_from_handle(handle);
  FP_RasterResult result = {0};
  if(cgfont != 0)
  {
    //- unpack font at pixel size (matches the freetype backend: pt @ 96 dpi)
    CGFloat pixel_size = (CGFloat)((96.f/72.f) * size);
    CTFontRef font = CTFontCreateWithGraphicsFont(cgfont, pixel_size, 0, 0);
    S32 ascent  = (S32)ceil_f64(CTFontGetAscent(font));
    S32 descent = (S32)ceil_f64(CTFontGetDescent(font));
    S32 leading = (S32)round_f64(CTFontGetLeading(font));
    S32 height  = ascent + descent + leading;

    //- build line
    CTLineRef line = 0;
    CGFloat width = 0;
    {
      CFStringRef cf_string = CFStringCreateWithBytes(kCFAllocatorDefault, string.str, (CFIndex)string.size, kCFStringEncodingUTF8, 0);
      if(cf_string != 0)
      {
        CFStringRef keys[] = {kCTFontAttributeName, kCTForegroundColorFromContextAttributeName};
        CFTypeRef values[] = {font, kCFBooleanTrue};
        CFDictionaryRef attributes = CFDictionaryCreate(kCFAllocatorDefault, (const void **)keys, (const void **)values, ArrayCount(keys),
                                                        &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
        CFAttributedStringRef attributed_string = CFAttributedStringCreate(kCFAllocatorDefault, cf_string, attributes);
        line = CTLineCreateWithAttributedString(attributed_string);
        width = (CGFloat)CTLineGetTypographicBounds(line, 0, 0, 0);
        CFRelease(attributed_string);
        CFRelease(attributes);
        CFRelease(cf_string);
      }
    }

    //- allocate atlas
    S32 atlas_width  = Clamp(1, (S32)ceil_f64(width) + 1, max_S16);
    S32 atlas_height = Clamp(1, height + 1, max_S16);
    Vec2S16 dim = {(S16)atlas_width, (S16)atlas_height};
    U64 atlas_size = (U64)dim.x * (U64)dim.y * 4;
    U8 *atlas = push_array(arena, U8, atlas_size);

    //- rasterize into alpha-only bitmap, then expand to white + coverage
    if(line != 0)
    {
      Temp scratch = scratch_begin(&arena, 1);
      U8 *alpha = push_array(scratch.arena, U8, (U64)dim.x * (U64)dim.y);
      CGContextRef ctx = CGBitmapContextCreate(alpha, (size_t)dim.x, (size_t)dim.y, 8, (size_t)dim.x, 0, (CGBitmapInfo)kCGImageAlphaOnly);
      if(ctx != 0)
      {
        B32 smooth = !!(flags & FP_RasterFlag_Smooth);
        CGContextSetShouldAntialias(ctx, smooth);
        CGContextSetAllowsAntialiasing(ctx, smooth);
        CGContextSetShouldSmoothFonts(ctx, 0);
        CGContextSetAllowsFontSubpixelQuantization(ctx, 0);
        CGContextSetShouldSubpixelQuantizeFonts(ctx, 0);
        CGContextSetGrayFillColor(ctx, 1, 1);
        CGContextSetTextMatrix(ctx, CGAffineTransformIdentity);

        // NOTE: CG's origin is the bottom-left; the baseline sits `ascent`
        // pixels below the top row, as in the freetype backend.
        CGContextSetTextPosition(ctx, 0, (CGFloat)(dim.y - ascent));
        CTLineDraw(line, ctx);
        CGContextFlush(ctx);
        CGContextRelease(ctx);

        // NOTE: bitmap context memory is stored top row first
        for(U64 idx = 0; idx < (U64)dim.x * (U64)dim.y; idx += 1)
        {
          atlas[idx*4+0] = 255;
          atlas[idx*4+1] = 255;
          atlas[idx*4+2] = 255;
          atlas[idx*4+3] = alpha[idx];
        }
      }
      scratch_end(scratch);
      CFRelease(line);
    }
    CFRelease(font);

    //- fill result
    result.atlas_dim = dim;
    result.advance   = (F32)width;
    result.atlas     = atlas;
  }
  ProfEnd();
  return result;
}
