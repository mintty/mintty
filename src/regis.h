
extern void regis_clear(HDC dc, int w, int h);
extern void regis_home(void);
extern void regis_draw(HDC dc, float scale, int w, int h, int mode, uchar * regis);

#define mock_regis

#ifdef mock_regis
#define regis_clear(dc, w, h)	
#define regis_home()	
#define regis_draw(dc, scale, w, h, mode, regis)	
#endif

