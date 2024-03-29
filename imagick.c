/* -*- Mode: C; Character-encoding: utf-8; -*- */

/* imagemagick.c
   This implements Kno bindings to the MagickWand API
   Copyright (C) 2012-2019 beingmeta, inc.
   Copyright (C) 2020-2022 beingmeta, LLC
*/

#ifndef _FILEINFO
#define _FILEINFO __FILE__
#endif

/* This avoids potential conflicts with OpenMP */
#define KNO_INLINE_REFCOUNTS 0

#include "kno/knosource.h"
#include "kno/lisp.h"
#include "kno/eval.h"
#include "kno/numbers.h"
#include "kno/sequences.h"
#include "kno/texttools.h"
#include "kno/cprims.h"

#include <libu8/libu8.h>
#include <libu8/u8printf.h>
#include <libu8/u8crypto.h>
#include <libu8/u8pathfns.h>

#include <wand/MagickWand.h>
#include <wand/pixel-wand.h>
#include <limits.h>
#include <ctype.h>
#include <math.h>

u8_condition MagickWandError="ImageMagicWand error";
kno_lisp_type kno_imagick_type;
#define KNO_IMAGICK_TYPE 0x1c3e8812

KNO_EXPORT int kno_init_imagick(void) KNO_LIBINIT_FN;

typedef struct KNO_IMAGICK {
  KNO_CONS_HEADER;
  MagickWand *wand;} KNO_IMAGICK;
typedef struct KNO_IMAGICK *kno_imagick;

/* Data for conversions */

static struct CTYPEMAP {
  CompressionType ct;
  char *cname; } compression_types[]={
  {NoCompression,"None"},
  {BZipCompression,"BZip"},
  {DXT1Compression,"DXT1"},
  {DXT3Compression,"DXT3"},
  {DXT5Compression,"DXT5"},
  {FaxCompression,"Fax"},
  {Group4Compression,"Group4"},
  {JPEGCompression,"JPEG"},
  {JPEG2000Compression,"JPEG2000"}, /* ISO/IEC std 15444-1 */
  {LosslessJPEGCompression,"LosslessJPEG"},
  {LZWCompression,"LZW"},
  {RLECompression,"RLE"},
  {ZipCompression,"Zip"},
#if (MagickLibVersion>0x670)
  {ZipSCompression,"ZipS"},
  {PizCompression,"Piz"},
  {Pxr24Compression,"Pxr24"},
  {B44Compression,"B44"},
  {B44ACompression,"B44A"},
  {LZMACompression,"LZMA"},             /* Lempel-Ziv-Markov chain algorithm */
  {JBIG1Compression,"JBIG1"},           /* ISO/IEC std 11544 / ITU-T rec T.82 */
  {JBIG2Compression,"JBIG2"},           /* ISO/IEC std 14492 / ITU-T rec T.88 */
#endif
  {UndefinedCompression,"Undefined"}};

static struct CSMAP {
  ColorspaceType cs;
  char *csname;} csmap[]={
  {RGBColorspace,"RGB"},
  {GRAYColorspace,"GRAY"},
  {TransparentColorspace,"Transparent"},
  {OHTAColorspace,"OHTA"},
  {LabColorspace,"Lab"},
  {XYZColorspace,"XYZ"},
  {YCbCrColorspace,"YCbCr"},
  {YCCColorspace,"YCC"},
  {YIQColorspace,"YIQ"},
  {YPbPrColorspace,"YPbPr"},
  {YUVColorspace,"YUV"},
  {CMYKColorspace,"CMYK"},
  {sRGBColorspace,"sRGB"},
  {HSBColorspace,"HSB"},
  {HSLColorspace,"HSL"},
  {HWBColorspace,"HWB"},
  {Rec601LumaColorspace,"Rec601Luma"},
  {Rec601YCbCrColorspace,"Rec601YCbCr"},
  {Rec709LumaColorspace,"Rec709Luma"},
  {Rec709YCbCrColorspace,"Rec709YCbCr"},
  {LogColorspace,"Log"},
  {CMYColorspace,"CMY"},
  /*
    {LuvColorspace,"Luv"},
    {HCLColorspace,"HCL"},
    {LCHColorspace,"LCH"},
    {LMSColorspace,"LMS"},
    {LCHabColorspace,"LCHab"},
    {LCHuvColorspace,"LCHuv"},
    {scRGBColorspace,"scRGB"},
  */
  {UndefinedColorspace,"Undefined"}};


char *ctype2string(CompressionType ct)
{
  struct CTYPEMAP *scan = compression_types;
  while (scan->ct!=UndefinedCompression)
    if (ct == scan->ct) return scan->cname;
    else scan++;
  return NULL;
}

char *cspace2string(ColorspaceType cs)
{
  struct CSMAP *scan = csmap;
  while (scan->cs!=UndefinedColorspace)
    if (cs == scan->cs) return scan->csname;
    else scan++;
  return NULL;
}


void magickwand_atexit()
{
  MagickWandTerminus();
}

void grabmagickerr(u8_context cxt,MagickWand *wand)
{
  ExceptionType severity;
  char *description = MagickGetException(wand,&severity);
  if (errno) u8_graberrno(cxt,NULL);
  u8_seterr(MagickWandError,cxt,u8_strdup(description));
  MagickRelinquishMemory(description);
  MagickClearException(wand);
}

static int unparse_imagick(struct U8_OUTPUT *out,lispval x)
{
  struct KNO_IMAGICK *wrapper = (struct KNO_IMAGICK *)x;
  u8_printf(out,"#<IMAGICK %lx>",((unsigned long)(wrapper->wand)));
  return 1;
}

static void recycle_imagick(struct KNO_RAW_CONS *c)
{
  struct KNO_IMAGICK *wrapper = (struct KNO_IMAGICK *)c;
  DestroyMagickWand(wrapper->wand);
  if (!(KNO_STATIC_CONSP(c))) u8_free(c);
}
DEFC_PRIM("file->imagick",file2imagick,
	  KNO_MAX_ARGS(1)|KNO_MIN_ARGS(1),
	  "**undocumented**",
	  {"arg",kno_string_type,KNO_VOID})


lispval file2imagick(lispval arg)
{
  MagickWand *wand;
  MagickBooleanType retval;
  struct KNO_IMAGICK *imagickref = u8_alloc(struct KNO_IMAGICK);
  KNO_INIT_FRESH_CONS(imagickref,kno_imagick_type);
  imagickref->wand = wand = NewMagickWand();
  retval = MagickReadImage(wand,KNO_CSTRING(arg));
  if (retval == MagickFalse) {
    grabmagickerr("file2imagick",wand);
    u8_free(imagickref);
    u8_free(wand);
    return KNO_ERROR_VALUE;}
  else {
    U8_CLEAR_ERRNO();
    return (lispval)imagickref;}
}
DEFC_PRIM("packet->imagick",packet2imagick,
	  KNO_MAX_ARGS(1)|KNO_MIN_ARGS(1),
	  "**undocumented**",
	  {"arg",kno_packet_type,KNO_VOID})

lispval packet2imagick(lispval arg)
{
  MagickWand *wand;
  MagickBooleanType retval;
  struct KNO_IMAGICK *imagickref = u8_alloc(struct KNO_IMAGICK);
  KNO_INIT_FRESH_CONS(imagickref,kno_imagick_type);
  imagickref->wand = wand = NewMagickWand();
  retval = MagickReadImageBlob
    (imagickref->wand,KNO_PACKET_DATA(arg),KNO_PACKET_LENGTH(arg));
  if (retval == MagickFalse) {
    grabmagickerr("file2imagick",wand);
    u8_free(imagickref);
    u8_free(wand);
    return KNO_ERROR_VALUE;}
  else {
    U8_CLEAR_ERRNO();
    return (lispval)imagickref;}
}
DEFC_PRIM("imagick->file",imagick2file,
	  KNO_MAX_ARGS(2)|KNO_MIN_ARGS(1),
	  "**undocumented**",
	  {"imagickref",KNO_IMAGICK_TYPE,KNO_VOID},
	  {"filename",kno_string_type,KNO_VOID})

lispval imagick2file(lispval imagickref,lispval filename)
{
  MagickBooleanType retval;
  struct KNO_IMAGICK *wrapper=
    kno_consptr(struct KNO_IMAGICK *,imagickref,kno_imagick_type);
  MagickWand *wand = wrapper->wand;
  retval = MagickWriteImage(wand,KNO_CSTRING(filename));
  if (retval == MagickFalse) {
    grabmagickerr("imagick2file",wand);
    return KNO_ERROR_VALUE;}
  else {
    U8_CLEAR_ERRNO();
    return kno_incref(imagickref);}
}
DEFC_PRIM("imagick->packet",imagick2packet,
	  KNO_MAX_ARGS(1)|KNO_MIN_ARGS(1),
	  "**undocumented**",
	  {"imagickref",KNO_IMAGICK_TYPE,KNO_VOID})

lispval imagick2packet(lispval imagickref)
{
  unsigned char *data = NULL; size_t n_bytes;
  struct KNO_IMAGICK *wrapper=
    kno_consptr(struct KNO_IMAGICK *,imagickref,kno_imagick_type);
  MagickWand *wand = wrapper->wand;
  MagickResetIterator(wand);
  data = MagickGetImageBlob(wand,&n_bytes);
  if (data == NULL) {
    grabmagickerr("imagick2packet",wand);
    return KNO_ERROR_VALUE;}
  else {
    lispval packet = kno_make_packet(NULL,n_bytes,data);
    MagickRelinquishMemory(data);
    U8_CLEAR_ERRNO();
    return packet;}
}
DEFC_PRIM("imagick/clone",imagick2imagick,
	  KNO_MAX_ARGS(1)|KNO_MIN_ARGS(1),
	  "**undocumented**",
	  {"imagickref",KNO_IMAGICK_TYPE,KNO_VOID})

lispval imagick2imagick(lispval imagickref)
{
  struct KNO_IMAGICK *wrapper=
    kno_consptr(struct KNO_IMAGICK *,imagickref,kno_imagick_type);
  struct KNO_IMAGICK *fresh = u8_alloc(struct KNO_IMAGICK);
  MagickWand *wand = CloneMagickWand(wrapper->wand);
  KNO_INIT_FRESH_CONS(fresh,kno_imagick_type);
  fresh->wand = wand;
  U8_CLEAR_ERRNO();
  return (lispval)fresh;
}

/* Getting properties */

static lispval format, resolution, size, width, height, interlace;
static lispval line_interlace, plane_interlace, partition_interlace;

static lispval imagick_table_get(lispval imagickref,lispval field,lispval dflt)
{
  /* enum result_type {imbool,imint,imdouble,imsize,imbox,imtrans} rt; */
  struct KNO_IMAGICK *wrapper=
    kno_consptr(struct KNO_IMAGICK *,imagickref,kno_imagick_type);
  MagickWand *wand = wrapper->wand;
  if (KNO_EQ(field,format)) {
    const char *fmt = MagickGetImageFormat(wand);
    return kno_mkstring((char *)fmt);}
#if (MagickLibVersion>0x670)
  else if (KNO_EQ(field,resolution)) {
    double x = 0, y = 0;
    /* MagickBooleanType rv = */
    MagickGetResolution(wand,&x,&y);
    return kno_conspair(kno_make_double(x),kno_make_double(y));}
#endif
  else if (KNO_EQ(field,interlace)) {
    InterlaceType it = MagickGetInterlaceScheme(wand);
    if (it == NoInterlace)
      return KNO_FALSE;
    else if (it == LineInterlace)
      return line_interlace;
    else if (it == PlaneInterlace)
      return plane_interlace;
    else if (it == PartitionInterlace)
      return partition_interlace;
    else return KNO_EMPTY_CHOICE;}
  else if (KNO_EQ(field,size)) {
    size_t w = MagickGetImageWidth(wand);
    size_t h = MagickGetImageHeight(wand);
    return kno_conspair(KNO_INT(w),KNO_INT(h));}
  else if (KNO_EQ(field,width)) {
    size_t w = MagickGetImageWidth(wand);
    return KNO_INT(w);}
  else if (KNO_EQ(field,height)) {
    size_t h = MagickGetImageHeight(wand);
    return KNO_INT(h);}
  else return KNO_VOID;
}

static FilterTypes default_filter = TriangleFilter;

static FilterTypes getfilter(lispval arg,u8_string cxt)
{
  u8_string name = NULL;
  if ((KNO_VOIDP(arg))||(KNO_FALSEP(arg)))
    return default_filter;
  else if (KNO_SYMBOLP(arg))
    name = KNO_SYMBOL_NAME(arg);
  else if (KNO_STRINGP(arg))
    name = KNO_CSTRING(arg);
  else name = NULL;
  if (name == NULL) {
    u8_log(LOG_WARN,cxt,"Bad filter arg %q",arg);
    return default_filter;}
  if (strcasecmp(name,"triangle")==0) return TriangleFilter;
  else if (strcasecmp(name,"box")==0) return BoxFilter;
  else if (strcasecmp(name,"blackman")==0) return BlackmanFilter;
  else if (strcasecmp(name,"catrom")==0) return CatromFilter;
  else if (strcasecmp(name,"gaussian")==0) return GaussianFilter;
  else if (strcasecmp(name,"cubic")==0) return CubicFilter;
  else if (strcasecmp(name,"hanning")==0) return HanningFilter;
  else if (strcasecmp(name,"hermite")==0) return HermiteFilter;
  else if (strcasecmp(name,"lanczos")==0) return LanczosFilter;
  else if (strcasecmp(name,"mitchell")==0) return MitchellFilter;
  else if (strcasecmp(name,"point")==0) return PointFilter;
  else if (strcasecmp(name,"quadratic")==0) return QuadraticFilter;
  else if (strcasecmp(name,"sinc")==0) return SincFilter;
  else if (strcasecmp(name,"bessel")==0) return BesselFilter;
  else {
    u8_log(LOG_WARN,cxt,"Bad filter arg %q",arg);
    return default_filter;}
}


DEFC_PRIM("imagick/format",imagick_format,
	  KNO_MAX_ARGS(2)|KNO_MIN_ARGS(2),
	  "**undocumented**",
	  {"imagickref",KNO_IMAGICK_TYPE,KNO_VOID},
	  {"format",kno_string_type,KNO_VOID})
static lispval imagick_format(lispval imagickref,lispval format)
{
  MagickBooleanType retval;
  struct KNO_IMAGICK *wrapper=
    kno_consptr(struct KNO_IMAGICK *,imagickref,kno_imagick_type);
  MagickWand *wand = wrapper->wand;
  retval = MagickSetImageFormat(wand,KNO_CSTRING(format));
  if (retval == MagickFalse) {
    grabmagickerr("imagick_format",wand);
    return KNO_ERROR_VALUE;}
  else return kno_incref(imagickref);
}


DEFC_PRIM("imagick/fit",imagick_fit,
	  KNO_MAX_ARGS(5)|KNO_MIN_ARGS(3),
	  "**undocumented**",
	  {"imagickref",KNO_IMAGICK_TYPE,KNO_VOID},
	  {"w_arg",kno_fixnum_type,KNO_VOID},
	  {"h_arg",kno_fixnum_type,KNO_VOID},
	  {"filter",kno_any_type,KNO_VOID},
	  {"blur",kno_flonum_type,KNO_VOID})
static lispval imagick_fit(lispval imagickref,lispval w_arg,lispval h_arg,
			   lispval filter,lispval blur)
{
  MagickBooleanType retval;
  struct KNO_IMAGICK *wrapper=
    kno_consptr(struct KNO_IMAGICK *,imagickref,kno_imagick_type);
  MagickWand *wand = wrapper->wand;
  if (!(KNO_UINTP(w_arg))) return kno_type_error("uint","imagick_fit",w_arg);
  else if (!(KNO_UINTP(h_arg))) return kno_type_error("uint","imagick_fit",h_arg);
  int width = KNO_FIX2INT(w_arg), height = KNO_FIX2INT(h_arg);
  size_t iwidth = MagickGetImageWidth(wand);
  size_t iheight = MagickGetImageHeight(wand);
  size_t target_width, target_height;
  double xscale = ((double)width)/((double)iwidth);
  double yscale = ((double)height)/((double)iheight);
  double scale = ((xscale<yscale)?(xscale):(yscale));
  target_width = (int)floor(iwidth*scale);
  target_height = (int)floor(iheight*scale);
  retval = MagickResizeImage
    (wand,target_width,target_height,
     getfilter(filter,"imagick_fit"),
     ((KNO_VOIDP(blur))?(1.0):(KNO_FLONUM(blur))));
  if (retval == MagickFalse) {
    grabmagickerr("imagick_fit",wand);
    return KNO_ERROR_VALUE;}
  else {
    U8_CLEAR_ERRNO();
    return kno_incref(imagickref);}
}


DEFC_PRIM("imagick/interlace",imagick_interlace,
	  KNO_MAX_ARGS(2)|KNO_MIN_ARGS(2),
	  "**undocumented**",
	  {"imagickref",KNO_IMAGICK_TYPE,KNO_VOID},
	  {"scheme",kno_any_type,KNO_VOID})
static lispval imagick_interlace(lispval imagickref,lispval scheme)
{
  MagickBooleanType retval;
  struct KNO_IMAGICK *wrapper=
    kno_consptr(struct KNO_IMAGICK *,imagickref,kno_imagick_type);
  MagickWand *wand = wrapper->wand;
  InterlaceType it;
  if ((KNO_FALSEP(scheme))||(KNO_VOIDP(scheme)))
    it = NoInterlace;
  else if (scheme == line_interlace) it = LineInterlace;
  else if (scheme == plane_interlace) it = PlaneInterlace;
  else if (scheme == partition_interlace) it = PartitionInterlace;
  else return kno_type_error
	 ("MagickWand Interlace type","imagick_interlace",scheme);
  retval = MagickSetInterlaceScheme(wand,it);
  if (retval == MagickFalse) {
    grabmagickerr("imagick_fit",wand);
    return KNO_ERROR_VALUE;}
  else {
    U8_CLEAR_ERRNO();
    return kno_incref(imagickref);}
}


DEFC_PRIM("imagick/extend",imagick_extend,
	  KNO_MAX_ARGS(6)|KNO_MIN_ARGS(3),
	  "**undocumented**",
	  {"imagickref",KNO_IMAGICK_TYPE,KNO_VOID},
	  {"w_arg",kno_fixnum_type,KNO_VOID},
	  {"h_arg",kno_fixnum_type,KNO_VOID},
	  {"x_arg",kno_fixnum_type,KNO_VOID},
	  {"y_arg",kno_fixnum_type,KNO_VOID},
	  {"bgcolor",kno_any_type,KNO_VOID})
static lispval imagick_extend(lispval imagickref,lispval w_arg,lispval h_arg,
			      lispval x_arg,lispval y_arg,
			      lispval bgcolor)
{
  MagickBooleanType retval;
  struct KNO_IMAGICK *wrapper=
    kno_consptr(struct KNO_IMAGICK *,imagickref,kno_imagick_type);
  MagickWand *wand = wrapper->wand;
  if (!(KNO_UINTP(w_arg))) return kno_type_error("uint","imagick_fit",w_arg);
  if (!(KNO_UINTP(h_arg))) return kno_type_error("uint","imagick_fit",h_arg);
  if (!(KNO_UINTP(x_arg))) return kno_type_error("uint","imagick_fit",x_arg);
  if (!(KNO_UINTP(y_arg))) return kno_type_error("uint","imagick_fit",y_arg);
  size_t width = KNO_FIX2INT(w_arg), height = KNO_FIX2INT(h_arg);
  size_t xoff = KNO_FIX2INT(x_arg), yoff = KNO_FIX2INT(y_arg);
  if (KNO_STRINGP(bgcolor)) {
    PixelWand *color = NewPixelWand();
    PixelSetColor(color,KNO_CSTRING(bgcolor));
    MagickSetImageBackgroundColor(wand,color);
    DestroyPixelWand(color);}
  retval = MagickExtentImage(wand,width,height,xoff,yoff);
  if (retval == MagickFalse) {
    grabmagickerr("imagick_extend",wand);
    return KNO_ERROR_VALUE;}
  else {
    U8_CLEAR_ERRNO();
    return kno_incref(imagickref);}
}


DEFC_PRIM("imagick/charcoal",imagick_charcoal,
	  KNO_MAX_ARGS(3)|KNO_MIN_ARGS(3),
	  "**undocumented**",
	  {"imagickref",KNO_IMAGICK_TYPE,KNO_VOID},
	  {"radius",kno_flonum_type,KNO_VOID},
	  {"sigma",kno_flonum_type,KNO_VOID})
static lispval imagick_charcoal(lispval imagickref,lispval radius,lispval sigma)
{
  MagickBooleanType retval;
  struct KNO_IMAGICK *wrapper=
    kno_consptr(struct KNO_IMAGICK *,imagickref,kno_imagick_type);
  MagickWand *wand = wrapper->wand;
  double r = KNO_FLONUM(radius), s = KNO_FLONUM(sigma);
  retval = MagickCharcoalImage(wand,r,s);
  if (retval == MagickFalse) {
    grabmagickerr("imagick_charcoal",wand);
    return KNO_ERROR_VALUE;}
  else {
    U8_CLEAR_ERRNO();
    return kno_incref(imagickref);}
}


DEFC_PRIM("imagick/emboss",imagick_emboss,
	  KNO_MAX_ARGS(3)|KNO_MIN_ARGS(3),
	  "**undocumented**",
	  {"imagickref",KNO_IMAGICK_TYPE,KNO_VOID},
	  {"radius",kno_flonum_type,KNO_VOID},
	  {"sigma",kno_flonum_type,KNO_VOID})
static lispval imagick_emboss(lispval imagickref,lispval radius,lispval sigma)
{
  MagickBooleanType retval;
  struct KNO_IMAGICK *wrapper=
    kno_consptr(struct KNO_IMAGICK *,imagickref,kno_imagick_type);
  MagickWand *wand = wrapper->wand;
  double r = KNO_FLONUM(radius), s = KNO_FLONUM(sigma);
  retval = MagickEmbossImage(wand,r,s);
  if (retval == MagickFalse) {
    grabmagickerr("imagick_emboss",wand);
    return KNO_ERROR_VALUE;}
  else {
    U8_CLEAR_ERRNO();
    return kno_incref(imagickref);}
}


DEFC_PRIM("imagick/blur",imagick_blur,
	  KNO_MAX_ARGS(3)|KNO_MIN_ARGS(3),
	  "**undocumented**",
	  {"imagickref",KNO_IMAGICK_TYPE,KNO_VOID},
	  {"radius",kno_flonum_type,KNO_VOID},
	  {"sigma",kno_flonum_type,KNO_VOID})
static lispval imagick_blur(lispval imagickref,lispval radius,lispval sigma)
{
  MagickBooleanType retval;
  struct KNO_IMAGICK *wrapper=
    kno_consptr(struct KNO_IMAGICK *,imagickref,kno_imagick_type);
  MagickWand *wand = wrapper->wand;
  double r = KNO_FLONUM(radius), s = KNO_FLONUM(sigma);
  retval = MagickGaussianBlurImage(wand,r,s);
  if (retval == MagickFalse) {
    grabmagickerr("imagick_blur",wand);
    return KNO_ERROR_VALUE;}
  else {
    U8_CLEAR_ERRNO();
    return kno_incref(imagickref);}
}


DEFC_PRIM("imagick/edge",imagick_edge,
	  KNO_MAX_ARGS(2)|KNO_MIN_ARGS(2),
	  "**undocumented**",
	  {"imagickref",KNO_IMAGICK_TYPE,KNO_VOID},
	  {"radius",kno_flonum_type,KNO_VOID})
static lispval imagick_edge(lispval imagickref,lispval radius)
{
  MagickBooleanType retval;
  struct KNO_IMAGICK *wrapper=
    kno_consptr(struct KNO_IMAGICK *,imagickref,kno_imagick_type);
  MagickWand *wand = wrapper->wand;
  double r = KNO_FLONUM(radius);
  retval = MagickEdgeImage(wand,r);
  if (retval == MagickFalse) {
    grabmagickerr("imagick_edge",wand);
    return KNO_ERROR_VALUE;}
  else {
    U8_CLEAR_ERRNO();
    return kno_incref(imagickref);}
}



DEFC_PRIM("imagick/crop",imagick_crop,
	  KNO_MAX_ARGS(5)|KNO_MIN_ARGS(3),
	  "**undocumented**",
	  {"imagickref",KNO_IMAGICK_TYPE,KNO_VOID},
	  {"width",kno_fixnum_type,KNO_VOID},
	  {"height",kno_fixnum_type,KNO_INT(0)},
	  {"xoff",kno_fixnum_type,KNO_INT(0)},
	  {"yoff",kno_fixnum_type,KNO_INT(0)})
static lispval imagick_crop(lispval imagickref,
			    lispval width,lispval height,
			    lispval xoff,lispval yoff)
{
  MagickBooleanType retval;
  struct KNO_IMAGICK *wrapper=
    kno_consptr(struct KNO_IMAGICK *,imagickref,kno_imagick_type);
  MagickWand *wand = wrapper->wand;
  size_t w = kno_getint(width), h = kno_getint(height);
  ssize_t x = kno_getint(xoff), y = kno_getint(yoff);
  retval = MagickCropImage(wand,w,h,x,y);
  if (retval == MagickFalse) {
    grabmagickerr("imagick_crop",wand);
    return KNO_ERROR_VALUE;}
  else {
    U8_CLEAR_ERRNO();
    return kno_incref(imagickref);}
}


DEFC_PRIM("imagick/flip",imagick_flip,
	  KNO_MAX_ARGS(1)|KNO_MIN_ARGS(1),
	  "**undocumented**",
	  {"imagickref",KNO_IMAGICK_TYPE,KNO_VOID})
static lispval imagick_flip(lispval imagickref)
{
  MagickBooleanType retval;
  struct KNO_IMAGICK *wrapper=
    kno_consptr(struct KNO_IMAGICK *,imagickref,kno_imagick_type);
  MagickWand *wand = wrapper->wand;
  retval = MagickFlipImage(wand);
  if (retval == MagickFalse) {
    grabmagickerr("imagick_flip",wand);
    return KNO_ERROR_VALUE;}
  else {
    U8_CLEAR_ERRNO();
    return kno_incref(imagickref);}
}


DEFC_PRIM("imagick/flop",imagick_flop,
	  KNO_MAX_ARGS(1)|KNO_MIN_ARGS(1),
	  "**undocumented**",
	  {"imagickref",KNO_IMAGICK_TYPE,KNO_VOID})
static lispval imagick_flop(lispval imagickref)
{
  MagickBooleanType retval;
  struct KNO_IMAGICK *wrapper=
    kno_consptr(struct KNO_IMAGICK *,imagickref,kno_imagick_type);
  MagickWand *wand = wrapper->wand;
  retval = MagickFlopImage(wand);
  if (retval == MagickFalse) {
    grabmagickerr("imagick_flop",wand);
    return KNO_ERROR_VALUE;}
  else {
    U8_CLEAR_ERRNO();
    return kno_incref(imagickref);}
}


DEFC_PRIM("imagick/equalize",imagick_equalize,
	  KNO_MAX_ARGS(1)|KNO_MIN_ARGS(1),
	  "**undocumented**",
	  {"imagickref",KNO_IMAGICK_TYPE,KNO_VOID})
static lispval imagick_equalize(lispval imagickref)
{
  MagickBooleanType retval;
  struct KNO_IMAGICK *wrapper=
    kno_consptr(struct KNO_IMAGICK *,imagickref,kno_imagick_type);
  MagickWand *wand = wrapper->wand;
  retval = MagickEqualizeImage(wand);
  if (retval == MagickFalse) {
    grabmagickerr("imagick_equalize",wand);
    return KNO_ERROR_VALUE;}
  else {
    U8_CLEAR_ERRNO();
    return kno_incref(imagickref);}
}


DEFC_PRIM("imagick/despeckle",imagick_despeckle,
	  KNO_MAX_ARGS(1)|KNO_MIN_ARGS(1),
	  "**undocumented**",
	  {"imagickref",KNO_IMAGICK_TYPE,KNO_VOID})
static lispval imagick_despeckle(lispval imagickref)
{
  MagickBooleanType retval;
  struct KNO_IMAGICK *wrapper=
    kno_consptr(struct KNO_IMAGICK *,imagickref,kno_imagick_type);
  MagickWand *wand = wrapper->wand;
  retval = MagickDespeckleImage(wand);
  if (retval == MagickFalse) {
    grabmagickerr("imagick_despeckle",wand);
    return KNO_ERROR_VALUE;}
  else {
    U8_CLEAR_ERRNO();
    return kno_incref(imagickref);}
}


DEFC_PRIM("imagick/enhance",imagick_enhance,
	  KNO_MAX_ARGS(1)|KNO_MIN_ARGS(1),
	  "**undocumented**",
	  {"imagickref",KNO_IMAGICK_TYPE,KNO_VOID})
static lispval imagick_enhance(lispval imagickref)
{
  MagickBooleanType retval;
  struct KNO_IMAGICK *wrapper=
    kno_consptr(struct KNO_IMAGICK *,imagickref,kno_imagick_type);
  MagickWand *wand = wrapper->wand;
  retval = MagickEnhanceImage(wand);
  if (retval == MagickFalse) {
    grabmagickerr("imagick_enhance",wand);
    return KNO_ERROR_VALUE;}
  else {
    U8_CLEAR_ERRNO();
    return kno_incref(imagickref);}
}


DEFC_PRIM("imagick/deskew",imagick_deskew,
	  KNO_MAX_ARGS(2)|KNO_MIN_ARGS(2),
	  "**undocumented**",
	  {"imagickref",KNO_IMAGICK_TYPE,KNO_VOID},
	  "threshold",kno_flonum_type,KNO_VOID)
static lispval imagick_deskew(lispval imagickref,lispval threshold)
{
  MagickBooleanType retval;
  struct KNO_IMAGICK *wrapper=
    kno_consptr(struct KNO_IMAGICK *,imagickref,kno_imagick_type);
  MagickWand *wand = wrapper->wand;
  double t = KNO_FLONUM(threshold);
  retval = MagickEdgeImage(wand,t);
  if (retval == MagickFalse) {
    grabmagickerr("imagick_deskew",wand);
    return KNO_ERROR_VALUE;}
  else {
    U8_CLEAR_ERRNO();
    return kno_incref(imagickref);}
}


DEFC_PRIM("imagick/display",imagick_display,
	  KNO_MAX_ARGS(2)|KNO_MIN_ARGS(1),
	  "**undocumented**",
	  {"imagickref",KNO_IMAGICK_TYPE,KNO_VOID},
	  {"display_name",kno_string_type,KNO_VOID})
static lispval imagick_display(lispval imagickref,lispval display_name)
{
  MagickBooleanType retval;
  struct KNO_IMAGICK *wrapper=
    kno_consptr(struct KNO_IMAGICK *,imagickref,kno_imagick_type);
  MagickWand *wand = wrapper->wand;
  u8_string display=
    ((KNO_VOIDP(display_name))?((u8_string)":0.0"):(KNO_CSTRING(display_name)));
  retval = MagickDisplayImage(wand,display);
  if (retval == MagickFalse) {
    grabmagickerr("imagick_display",wand);
    return KNO_ERROR_VALUE;}
  else {
    U8_CLEAR_ERRNO();
    return kno_incref(imagickref);}
}


DEFC_PRIM("imagick/get",imagick_get,
	  KNO_MAX_ARGS(3)|KNO_MIN_ARGS(2),
	  "**undocumented**",
	  {"imagickref",KNO_IMAGICK_TYPE,KNO_VOID},
	  {"property",kno_any_type,KNO_VOID},
	  {"dflt",kno_any_type,KNO_VOID})
static lispval imagick_get(lispval imagickref,lispval property,lispval dflt)
{
  struct KNO_IMAGICK *wrapper=
    kno_consptr(struct KNO_IMAGICK *,imagickref,kno_imagick_type);
  MagickWand *wand = wrapper->wand;
  const char *pname = ((KNO_SYMBOLP(property))?(KNO_SYMBOL_NAME(property)):
		       (KNO_STRINGP(property))?(KNO_CSTRING(property)):(NULL));
  char *value = MagickGetImageProperty(wand,pname);
  if (value) {
    lispval stringval = kno_make_string(NULL,-1,value);
    MagickRelinquishMemory(value);
    return stringval;}
  else if (KNO_VOIDP(dflt))
    return KNO_EMPTY_CHOICE;
  else return kno_incref(dflt);
}


DEFC_PRIM("imagick/keys",imagick_getkeys,
	  KNO_MAX_ARGS(1)|KNO_MIN_ARGS(1),
	  "**undocumented**",
	  {"imagickref",KNO_IMAGICK_TYPE,KNO_VOID})
static lispval imagick_getkeys(lispval imagickref)
{
  struct KNO_IMAGICK *wrapper=
    kno_consptr(struct KNO_IMAGICK *,imagickref,kno_imagick_type);
  MagickWand *wand = wrapper->wand;
  size_t n_keys = 0;
  char **properties = MagickGetImageProperties(wand,"",&n_keys);
  if (properties) {
    lispval results = KNO_EMPTY_CHOICE;
    int i = 0; while (i<n_keys) {
      char *pname = properties[i++];
      lispval key = kno_make_string(NULL,-1,pname);
      KNO_ADD_TO_CHOICE(results,key);}
    MagickRelinquishMemory(properties);
    return results;}
  else return KNO_VOID;
}

static long long int imagick_init = 0;

static void init_symbols()
{
  format = kno_intern("format");
  resolution = kno_intern("resolution");

  size = kno_intern("size");
  width = kno_intern("width");
  height = kno_intern("height");
  interlace = kno_intern("interlace");
  line_interlace = kno_intern("line");
  plane_interlace = kno_intern("plane");
  partition_interlace = kno_intern("parition");

}

static lispval imagick_module;

int kno_init_imagick()
{
  if (imagick_init) return 0;
  else imagick_init = u8_millitime();
  imagick_module = kno_new_cmodule("imagick",0,kno_init_imagick);

  kno_imagick_type = kno_register_cons_type("imagick",KNO_IMAGICK_TYPE);
  kno_unparsers[kno_imagick_type]=unparse_imagick;
  kno_recyclers[kno_imagick_type]=recycle_imagick;

  init_symbols();

  kno_tablefns[kno_imagick_type]=u8_zalloc(struct KNO_TABLEFNS);
  kno_tablefns[kno_imagick_type]->get = (kno_table_get_fn)imagick_table_get;
  kno_tablefns[kno_imagick_type]->add = NULL;
  kno_tablefns[kno_imagick_type]->drop = NULL;
  kno_tablefns[kno_imagick_type]->store = NULL;
  kno_tablefns[kno_imagick_type]->test = NULL;
  kno_tablefns[kno_imagick_type]->getsize = NULL;
  kno_tablefns[kno_imagick_type]->keys = NULL;

  link_local_cprims();

  MagickWandGenesis();
  atexit(magickwand_atexit);

  U8_DISCARD_ERRNO(2);

  return 1;

}

static void link_local_cprims()
{
  KNO_LINK_CPRIM("imagick/keys",imagick_getkeys,1,imagick_module);
  KNO_LINK_CPRIM("imagick/get",imagick_get,3,imagick_module);
  KNO_LINK_CPRIM("imagick/display",imagick_display,2,imagick_module);
  KNO_LINK_CPRIM("imagick/deskew",imagick_deskew,2,imagick_module);
  KNO_LINK_CPRIM("imagick/enhance",imagick_enhance,1,imagick_module);
  KNO_LINK_CPRIM("imagick/despeckle",imagick_despeckle,1,imagick_module);
  KNO_LINK_CPRIM("imagick/equalize",imagick_equalize,1,imagick_module);
  KNO_LINK_CPRIM("imagick/flop",imagick_flop,1,imagick_module);
  KNO_LINK_CPRIM("imagick/flip",imagick_flip,1,imagick_module);
  KNO_LINK_CPRIM("imagick/crop",imagick_crop,5,imagick_module);
  KNO_LINK_CPRIM("imagick/edge",imagick_edge,2,imagick_module);
  KNO_LINK_CPRIM("imagick/blur",imagick_blur,3,imagick_module);
  KNO_LINK_CPRIM("imagick/emboss",imagick_emboss,3,imagick_module);
  KNO_LINK_CPRIM("imagick/charcoal",imagick_charcoal,3,imagick_module);
  KNO_LINK_CPRIM("imagick/extend",imagick_extend,6,imagick_module);
  KNO_LINK_CPRIM("imagick/interlace",imagick_interlace,2,imagick_module);
  KNO_LINK_CPRIM("imagick/fit",imagick_fit,5,imagick_module);
  KNO_LINK_CPRIM("imagick/format",imagick_format,2,imagick_module);
  KNO_LINK_CPRIM("imagick/clone",imagick2imagick,1,imagick_module);
  KNO_LINK_CPRIM("imagick->packet",imagick2packet,1,imagick_module);
  KNO_LINK_CPRIM("imagick->file",imagick2file,2,imagick_module);
  KNO_LINK_CPRIM("packet->imagick",packet2imagick,1,imagick_module);
  KNO_LINK_CPRIM("file->imagick",file2imagick,1,imagick_module);
  KNO_LINK_CPRIM("imagick->file",imagick2file,2,imagick_module);
  KNO_LINK_CPRIM("imagick->packet",imagick2packet,1,imagick_module);
  KNO_LINK_CPRIM("imagick/clone",imagick2imagick,1,imagick_module);
  KNO_LINK_CPRIM("imagick/format",imagick_format,2,imagick_module);
  KNO_LINK_CPRIM("imagick/fit",imagick_fit,5,imagick_module);
  KNO_LINK_CPRIM("imagick/interlace",imagick_interlace,2,imagick_module);
  KNO_LINK_CPRIM("imagick/extend",imagick_extend,6,imagick_module);
  KNO_LINK_CPRIM("imagick/charcoal",imagick_charcoal,3,imagick_module);
  KNO_LINK_CPRIM("imagick/emboss",imagick_emboss,3,imagick_module);
  KNO_LINK_CPRIM("imagick/blur",imagick_blur,3,imagick_module);
  KNO_LINK_CPRIM("imagick/edge",imagick_edge,2,imagick_module);
  KNO_LINK_CPRIM("imagick/crop",imagick_crop,5,imagick_module);
  KNO_LINK_CPRIM("imagick/flip",imagick_flip,1,imagick_module);
  KNO_LINK_CPRIM("imagick/flop",imagick_flop,1,imagick_module);
  KNO_LINK_CPRIM("imagick/equalize",imagick_equalize,1,imagick_module);
  KNO_LINK_CPRIM("imagick/despeckle",imagick_despeckle,1,imagick_module);
  KNO_LINK_CPRIM("imagick/enhance",imagick_enhance,1,imagick_module);
  KNO_LINK_CPRIM("imagick/deskew",imagick_deskew,2,imagick_module);
  KNO_LINK_CPRIM("imagick/display",imagick_display,2,imagick_module);
  KNO_LINK_CPRIM("imagick/get",imagick_get,3,imagick_module);
  KNO_LINK_CPRIM("imagick/keys",imagick_getkeys,1,imagick_module);
}
