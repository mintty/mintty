
typedef void (*flush_fn)(void);

extern void regis_draw(HDC dc, float scale, int w, int h, int mode, uchar * regis, flush_fn flush);

//#define debug_regis
#define mock_regis

#ifdef mock_regis
#define regis_draw(dc, scale, w, h, mode, regis, flush)	(void)flush; (void)regarg;
#endif

