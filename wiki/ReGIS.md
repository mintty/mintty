
## Mintty ReGIS vector graphics support ##

The ReGIS remote graphics format was used in the 1980s for CAD and 
data visualisation applications. It was supported by some DEC terminals 
up to the VT340, so its support is a completion for emulation of 
DEC terminals as well as xterm.

The mintty home page shows two sample screenshots.
An archive with [sample files](regis-samples.zip) is here for download.

ReGIS features are supported with the following modifications:

### Unsupported or limited features ###

* Incremental rendering (with ReGIS introduction code ESC P 0 p) and 
  combination of multiple ReGIS graphics are not supported. 
  However, subsequent settings (cursor, colour) and macro definitions persist.
* Most S screen control options and sub-commands are not supported, 
  except background colour and erase I(),E and time delay T which 
  are supported.
* W(F) planes are not supported.
* W(C) writing style Complement is not supported.
* P(P) graphics pages are not supported.
* T(A) character set selection is not supported.
* T(S[]) T(U) T(H) T(M) character size settings are not supported.
* T[] character spacing is not supported.
* T(I) slant angles other than italic are not supported.
* T(...,D) character tilt is not supported.
* L character set loading is not supported.
* R reporting and graphics input interaction are not supported.

### Modified or extended features ###

* Default graphics output is inline at current cursor position rather than 
  fixed at screen top position (switchable by Sixel Display mode).
* On incremental output of multiple ReGIS graphics (ESC P 0 p):
  properties of subsequent output may get interlaced with 
  refreshed output of previous (e.g. scrolled) graphics.
* Colour values support RGB values like I(R50G20B90) in addition to HLS.
* Graphics size is scaled to the terminal width when output, 
  and later scaled when zooming the window;
  due to scaling, pixels/lines shown side-by-side may render with gaps.
* W(S) shading is approximated by filling. This fails in a few border cases.
* W(S'x') shading characters are mapped to library hatch patterns.
* W(Pn,S1) pattern brush is approximated by hatch brush styles.
* T(I<-angle>) italic (negative angle) uses italic font.
* T(D) string tilt is not limited to 45° steps.
* T(W(E)) or T(W(C)) writes with background colour.
* T"..." text is interpreted in current encoding.
* T"..." supports Unicode, wide and combining characters.
* T"...^J..." gets a ^M inserted unless `stty -onlcr` was set during output.
* S(T) time delay is accelerated by 4 like in xterm.
* @ macrographs are supported.
* # begins a comment to the end of the line.

