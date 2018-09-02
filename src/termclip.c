// termclip.c (part of mintty)
// Copyright 2008-10 Andy Koppe, 2017 Thomas Wolff
// Adapted from code from PuTTY-0.60 by Simon Tatham and team.
// Licensed under the terms of the GNU General Public License v3 or later.

#include "termpriv.h"

#include "win.h"
#include "child.h"
#include "charset.h"

typedef struct {
  size_t capacity;  // number of items allocated for text/cattrs
  size_t len;    // number of actual items at text/cattrs (inc. null terminator)
  wchar *text;   // text to copy (eventually null terminated)
  cattr *cattrs; // matching cattr for each wchar of text
} clip_workbuf;

static void
destroy_clip_workbuf(clip_workbuf * b)
{
  assert(b && b->capacity); // we're only called after get_selection, which always allocates
  free(b->text);
  free(b->cattrs);
  free(b);
}

// All b members must be 0 initially, ca may be null if the caller doesn't care
static void
clip_addchar(clip_workbuf * b, wchar chr, cattr * ca)
{
  if (b->len >= b->capacity) {
    b->capacity = b->len ? b->len * 2 : 1024;  // x2 strategy, 1K chars initially
    b->text = renewn(b->text, b->capacity);
    b->cattrs = renewn(b->cattrs, b->capacity);
  }

  b->text[b->len] = chr;
  b->cattrs[b->len] = ca ? *ca : (cattr){0, 0, 0, 0};
  b->len++;
}

// except OOM, guaranteed at least emtpy null terminated wstring and one cattr
static clip_workbuf*
get_selection(pos start, pos end, bool rect, bool allinline)
{
  int old_top_x = start.x;    /* needed for rect==1 */
  clip_workbuf *buf = newn(clip_workbuf, 1);
  *buf = (clip_workbuf){0, 0, 0, 0};  // all members to 0 initially

  while (poslt(start, end)) {
    bool nl = false;
    termline *line = fetch_line(start.y);
    pos nlpos;
    wchar * sixel_clipp = (wchar *)cfg.sixel_clip_char;

   /*
    * nlpos will point at the maximum position on this line we
    * should copy up to. So we start it at the end of the line...
    */
    nlpos.y = start.y;
    nlpos.x = term.cols;
    nlpos.r = false;

   /*
    * ... move it backwards if there's unused space at the end
    * of the line (and also set `nl' if this is the case,
    * because in normal selection mode this means we need a
    * newline at the end)...
    */
    if (allinline) {
      if (poslt(nlpos, end))
        nl = true;
    }
    else if (!(line->lattr & LATTR_WRAPPED)) {
      while (nlpos.x && line->chars[nlpos.x - 1].chr == ' ' &&
             !line->chars[nlpos.x - 1].cc_next && poslt(start, nlpos))
        decpos(nlpos);
      if (poslt(nlpos, end))
        nl = true;
    }
    else if (line->lattr & LATTR_WRAPPED2) {
     /* Ignore the last char on the line in a WRAPPED2 line. */
      decpos(nlpos);
    }

   /*
    * ... and then clip it to the terminal x coordinate if
    * we're doing rectangular selection. (In this case we
    * still did the above, so that copying e.g. the right-hand
    * column from a table doesn't fill with spaces on the right.)
    */
    if (rect) {
      if (nlpos.x > end.x)
        nlpos.x = end.x;
      nl = (start.y < end.y);
    }

    while (poslt(start, end) && poslt(start, nlpos)) {
      wchar cbuf[16], *p;
      int x = start.x;

      if (line->chars[x].chr == UCSWIDE) {
        start.x++;
        continue;
      }

      while (1) {
        wchar c = line->chars[x].chr;
        cattr *pca = &line->chars[x].attr;
        if (c == SIXELCH && *cfg.sixel_clip_char) {
          // copy replacement into clipboard
          if (!*sixel_clipp)
            sixel_clipp = (wchar *)cfg.sixel_clip_char;
          c = *sixel_clipp++;
        }
        else
          sixel_clipp = (wchar *)cfg.sixel_clip_char;
        cbuf[0] = c;
        cbuf[1] = 0;

        for (p = cbuf; *p; p++)
          clip_addchar(buf, *p, pca);

        if (line->chars[x].cc_next)
          x += line->chars[x].cc_next;
        else
          break;
      }
      start.x++;
    }
    if (nl) {
      clip_addchar(buf, '\r', 0);
      clip_addchar(buf, '\n', 0);
    }
    start.y++;
    start.x = rect ? old_top_x : 0;

    release_line(line);
  }
  clip_addchar(buf, 0, 0);
  return buf;
}

void
term_copy(void)
{
  if (!term.selected)
    return;

  clip_workbuf *buf = get_selection(term.sel_start, term.sel_end, term.sel_rect, false);
  win_copy(buf->text, buf->cattrs, buf->len);
  destroy_clip_workbuf(buf);
}

void
term_open(void)
{
  if (!term.selected)
    return;
  clip_workbuf *buf = get_selection(term.sel_start, term.sel_end, term.sel_rect, false);

  // Don't bother opening if it's all whitespace.
  wchar *p = buf->text;
  while (iswspace(*p))
    p++;
  if (*p)
    win_open(wcsdup(buf->text));  // win_open frees its argument

  destroy_clip_workbuf(buf);
}

void
term_paste(wchar *data, uint len)
{
  term_cancel_paste();

  term.paste_buffer = newn(wchar, len);
  term.paste_len = term.paste_pos = 0;

  // Copy data to the paste buffer, converting both Windows-style \r\n and
  // Unix-style \n line endings to \r, because that's what the Enter key sends.
  for (uint i = 0; i < len; i++) {
    wchar wc = data[i];
    if (wc != '\n')
      term.paste_buffer[term.paste_len++] = wc;
    else if (i == 0 || data[i - 1] != '\r')
      term.paste_buffer[term.paste_len++] = '\r';
  }

  if (term.bracketed_paste)
    child_write("\e[200~", 6);
  term_send_paste();
}

void
term_cancel_paste(void)
{
  if (term.paste_buffer) {
    free(term.paste_buffer);
    term.paste_buffer = 0;
    if (term.bracketed_paste)
      child_write("\e[201~", 6);
  }
}

void
term_send_paste(void)
{
  int i = term.paste_pos;
  while (i < term.paste_len && term.paste_buffer[i++] != '\r');
  child_sendw(term.paste_buffer + term.paste_pos, i - term.paste_pos);
  if (i < term.paste_len)
    term.paste_pos = i;
  else
    term_cancel_paste();
}

void
term_select_all(void)
{
  term.sel_start = (pos){-sblines(), 0, false};
  term.sel_end = (pos){term_last_nonempty_line(), term.cols, true};
  term.selected = true;
  if (cfg.copy_on_select)
    term_copy();
}

#define dont_debug_user_cmd_clip

static wchar *
term_get_text(bool all, bool screen, bool command)
{
  pos start;
  pos end;
  bool rect = false;

  if (command) {
    int sbtop = -sblines();
    int y = term_last_nonempty_line();
    bool skipprompt = true;  // skip upper lines of multi-line prompt

    if (y < sbtop) {
      y = sbtop;
      end = (pos){y, 0, false};
    }
    else {
      termline * line = fetch_line(y);
      if (line->lattr & LATTR_MARKED) {
        if (y > sbtop) {
          y--;
          end = (pos){y, term.cols, false};
          termline * line = fetch_line(y);
          if (line->lattr & LATTR_MARKED)
            y++;
        }
        else {
          end = (pos){y, 0, false};
        }
      }
      else {
        skipprompt = line->lattr & LATTR_UNMARKED;
        end = (pos){y, term.cols, false};
      }

      if (fetch_line(y)->lattr & LATTR_UNMARKED)
        end = (pos){y, 0, false};
    }

    int yok = y;
    while (y-- > sbtop) {
      termline * line = fetch_line(y);
#ifdef debug_user_cmd_clip
      printf("y %d skip %d marked %X\n", y, skipprompt, line->lattr & (LATTR_UNMARKED | LATTR_MARKED));
#endif
      if (skipprompt && (line->lattr & LATTR_UNMARKED))
        end = (pos){y, 0, false};
      else
        skipprompt = false;
      if (line->lattr & LATTR_MARKED) {
        break;
      }
      yok = y;
    }
    start = (pos){yok, 0, false};
#ifdef debug_user_cmd_clip
    printf("%d:%d...%d:%d\n", start.y, start.x, end.y, end.x);
#endif
  }
  else if (screen) {
    start = (pos){term.disptop, 0, false};
    end = (pos){term_last_nonempty_line(), term.cols, false};
  }
  else if (all) {
    start = (pos){-sblines(), 0, false};
    end = (pos){term_last_nonempty_line(), term.cols, false};
  }
  else if (!term.selected) {
    return wcsdup(W(""));
  }
  else {
    start = term.sel_start;
    end = term.sel_end;
    rect = term.sel_rect;
  }

  clip_workbuf *buf = get_selection(start, end, rect, false);
  wchar * tbuf = wcsdup(buf->text);
  destroy_clip_workbuf(buf);
  return tbuf;
}

void
term_cmd(char * cmdpat)
{
  // provide scrollback buffer
  wchar * wsel = term_get_text(true, false, false);
  char * sel = cs__wcstombs(wsel);
  free(wsel);
  setenv("MINTTY_BUFFER", sel, true);
  free(sel);
  // provide current selection
  wsel = term_get_text(false, false, false);
  sel = cs__wcstombs(wsel);
  free(wsel);
  setenv("MINTTY_SELECT", sel, true);
  free(sel);
  // provide current screen
  wsel = term_get_text(false, true, false);
  sel = cs__wcstombs(wsel);
  free(wsel);
  setenv("MINTTY_SCREEN", sel, true);
  free(sel);
  // provide last command output
  wsel = term_get_text(false, false, true);
  sel = cs__wcstombs(wsel);
  free(wsel);
  setenv("MINTTY_OUTPUT", sel, true);
  free(sel);
  // provide window title
  char * ttl = win_get_title();
  setenv("MINTTY_TITLE", ttl, true);
  free(ttl);

#ifdef use_placeholders
  sel = 0;
  if (strstr(cmdpat, "%s") || strstr(cmdpat, "%1$s")) {
    wchar * wsel = term_get_text(false, false, false);
    sel = cs__wcstombs(wsel);
    free(wsel);
  }

  int len = strlen(cmdpat) + (sel ? strlen(sel) : 0) + 1;
  char * cmd = newn(char, len);
  sprintf(cmd, cmdpat, sel ?: "");
  if (sel)
    free(sel);
#else
  char * cmd = cmdpat;
#endif

  FILE * cmdf = popen(cmd, "r");
  unsetenv("MINTTY_TITLE");
  unsetenv("MINTTY_OUTPUT");
  unsetenv("MINTTY_SCREEN");
  unsetenv("MINTTY_SELECT");
  unsetenv("MINTTY_BUFFER");
  unsetenv("MINTTY_CWD");
  unsetenv("MINTTY_PID");
  unsetenv("MINTTY_PROG");
  if (cmdf) {
    if (term.bracketed_paste)
      child_write("\e[200~", 6);
    char line[222];
    while (fgets(line, sizeof line, cmdf)) {
      child_send(line, strlen(line));
    }
    pclose(cmdf);
    if (term.bracketed_paste)
      child_write("\e[201~", 6);
  }
}

#include <time.h>
#include <sys/time.h>
#include <fcntl.h>
#include "winpriv.h"  // PADDING

void
term_export_html(bool do_open)
{
  struct timeval now;
  gettimeofday(& now, 0);
  char * htmlf = newn(char, MAX_PATH + 1);
  strftime(htmlf, MAX_PATH, "mintty.%F_%T.html", localtime (& now.tv_sec));

  int hfd = open(htmlf, O_WRONLY | O_CREAT | O_EXCL, 0600);
  if (hfd < 0) {
    win_bell(&cfg);
    return;
  }
  FILE * hf = fdopen(hfd, "w");
  if (!hf) {
    win_bell(&cfg);
    return;
  }

  pos start = term.sel_start;
  pos end = term.sel_end;
  bool rect = term.sel_rect;
  if (!term.selected) {
    start = (pos){term.disptop, 0, false};
    end = (pos){term.disptop + term.rows - 1, term.cols, false};
    rect = false;
  }

  char * font_name = cs__wcstoutf(cfg.font.name);
  colour fg_colour = win_get_colour(FG_COLOUR_I);
  colour bg_colour = win_get_colour(BG_COLOUR_I);
  colour bold_colour = win_get_colour(BOLD_COLOUR_I);
  fprintf(hf,
    "<head>\n"
    "  <meta name='generator' content='mintty'/>\n"
    "  <meta http-equiv='Content-Type' content='text/html; charset=UTF-8'/>\n"
    "  <title>mintty screen dump</title>\n"
    "  <link rel='stylesheet' type='text/css' href='xterm.css'/>\n"
    "  <link rel='stylesheet' type='text/css' href='mintty.css'/>\n"
    //"  <script type='text/javascript' language='JavaScript' src='emoji.js'></script>\n"
    "  <style type='text/css'>\n"
    "  body, pre { margin: 0; padding: 0; }\n"
    "  pre { font-family: inherit; }\n"
    );
  if (cfg.underl_colour != (colour)-1)
    fprintf(hf, "  span { text-decoration-color: #%02X%02X%02X; }\n",
            red(cfg.underl_colour), green(cfg.underl_colour), blue(cfg.underl_colour));
  fprintf(hf,
    "  #vt100 {\n"
    "    float: left;\n"
    "    border: 0px solid;\n"
    "    padding: %dpx;\n"
    "    line-height: %d%%;\n"
    "    font-size: %dpt;\n"
    "    font-family: '%s', 'Lucida Console ', 'Consolas';\n"
                            // ? 'Lucida Sans Typewriter', 'Courier New', 'Courier'
    "    color: #%02X%02X%02X;\n",
    PADDING, line_scale, font_size, font_name,
    red(fg_colour), green(fg_colour), blue(fg_colour));
  free(font_name);

  if (*cfg.background && !term.selected) {
    wstring wbg = cfg.background;
    bool tiled = *wbg == '*';
    if (*wbg == '*' || *wbg == '_')
      wbg++;
    char * bg = cs__wcstoutf(wbg);
    int alpha = -1;
    char * salpha = strrchr(bg, ',');
    if (salpha) {
      *salpha = 0;
      salpha++;
      sscanf(salpha, "%u%c", &alpha, &(char){0});
    }

    if (alpha >= 0) {
      fprintf(hf, "    }\n");
      fprintf(hf, "    #vt100 pre {\n");
      fprintf(hf, "      background-color: rgba(%d, %d, %d, %.3f);\n",
              red(bg_colour), green(bg_colour), blue(bg_colour),
              (255.0 - alpha) / 255);
      fprintf(hf, "    }\n");
      fprintf(hf, "    td {\n");
    }

    fprintf(hf, "    background-image: url('%s');\n", bg);
    if (!tiled) {
      fprintf(hf, "    background-attachment: no-repeat;\n");
      fprintf(hf, "    background-size: 100%% 100%%;\n");
    }

    free(bg);
  }
  else
  {
    fprintf(hf, "    background-color: #%02X%02X%02X;\n",
            red(bg_colour), green(bg_colour), blue(bg_colour));
  }
  fprintf(hf, "    }\n");
  fprintf(hf, "  .bd { font-weight: bold }\n");
  fprintf(hf, "  .it { font-style: italic }\n");
  fprintf(hf, "  .ul { text-decoration-line: underline }\n");
  fprintf(hf, "  .st { text-decoration-line: line-through }\n");
  fprintf(hf, "  .lu { text-decoration-line: line-through underline }\n");
  if (bold_colour != (colour)-1)
    fprintf(hf, "  .bold-color { color: #%02X%02X%02X }\n",
            red(bold_colour), green(bold_colour), blue(bold_colour));
  for (int i = 0; i < 16; i++) {
    colour ansii = win_get_colour(ANSI0 + i);
    uchar r = red(ansii), g = green(ansii), b = blue(ansii);
    fprintf(hf, "  .fg-color%d { color: #%02X%02X%02X }"
                " .bg-color%d { background-color: #%02X%02X%02X }\n",
                i, r, g, b, i, r, g, b);
  }
#ifdef support_cursor
  colour cursor_colour = win_get_colour(CURSOR_COLOUR_I);
  fprintf(hf, "  .cursor { background-color: #%02X%02X%02X }\n",
          red(cursor_colour), green(cursor_colour), blue(cursor_colour));
#endif

  for (int i = 1; i <= 10; i++)
    if (*cfg.fontfams[i].name) {
      char * fn = cs__wcstoutf(cfg.fontfams[i].name);
      fprintf(hf, "  .font%d { font-family: '%s' }\n", i, fn);
      free(fn);
    }
  if (!*cfg.fontfams[10].name)
    fprintf(hf, "  .font10 { font-family: 'F25 Blackletter Typewriter' }\n");
  fprintf(hf, "  </style>\n");
  fprintf(hf, "  <script>\n");
  fprintf(hf, "  var b1 = 500; var b2 = 300;\n");
  fprintf(hf, "  function visib (tag, state, timeout) {\n");
  fprintf(hf, "    var bl = document.getElementsByName(tag);\n");
  fprintf(hf, "    var vv; if (state) vv = 'visible'; else vv = 'hidden';\n");
  fprintf(hf, "    var i;\n");
  fprintf(hf, "    for (i = 0; i < bl.length; i++) {\n");
  fprintf(hf, "      bl[i].style.visibility = vv;\n");
  fprintf(hf, "    }\n");
  fprintf(hf, "    window.setTimeout ('visib (\"' + tag + '\", ' + !state + ', ' + timeout + ')', timeout);\n");
  fprintf(hf, "  }\n");
  fprintf(hf, "  function setup () {\n");
  fprintf(hf, "    window.setTimeout ('visib (\"blink\", 0, b1)', b1);\n");
  fprintf(hf, "    window.setTimeout ('visib (\"rapid\", 0, b2)', b2);\n");
  fprintf(hf, "  }\n");
  fprintf(hf, "  </script>\n");
  fprintf(hf, "</head>\n\n");
  fprintf(hf, "<body onload='setup();'>\n");
  fprintf(hf, "  <table border=0 cellpadding=0 cellspacing=0><tr><td xbackground=>\n");
  fprintf(hf, "  <div id='vt100'>\n");
  fprintf(hf, "   <pre>");

  clip_workbuf *buf = get_selection(start, end, rect, true);
  int i0 = 0;
  bool odd = true;
  for (uint i = 0; i < buf->len; i++) {
    if (!buf->text[i] || buf->text[i] == '\r' || buf->text[i] == '\n'
        // buf->cattrs[i] ~!= buf->cattrs[i0] ?
        // we need to check more than termattrs_equal_fg
        // but less than termchars_equal_override
# define IGNATTR (DATTR_MASK | ATTR_WIDE | TATTR_COMBINING)
        || (buf->cattrs[i].attr & ~IGNATTR) != (buf->cattrs[i0].attr & ~IGNATTR)
        || buf->cattrs[i].truefg != buf->cattrs[i0].truefg
        || buf->cattrs[i].truebg != buf->cattrs[i0].truebg
        || buf->cattrs[i].ulcolr != buf->cattrs[i0].ulcolr
       )
    {
      // flush chunk with equal attributes
      fprintf(hf, "<span class='%s", odd ? "od" : "ev");

      // retrieve chunk
      wchar save = buf->text[i];
      buf->text[i] = 0;
      char * s = cs__wcstoutf(&buf->text[i0]);
      buf->text[i] = save;

      cattr * ca = &buf->cattrs[i0];
      int fgi = (ca->attr & ATTR_FGMASK) >> ATTR_FGSHIFT;
      int bgi = (ca->attr & ATTR_BGMASK) >> ATTR_BGSHIFT;
      bool dim = ca->attr & ATTR_DIM;
      bool rev = ca->attr & ATTR_REVERSE;

      // colour setup preparations;
      // we could perhaps reuse apply_attr_colour here, but again
      // the situation is specific: some terminal handling (manual bolding)
      // is not applicable in HTML export, and we do not want to simply
      // always retrieve a plain colour value because we want to specify
      // colour style or class only if the respective default is overridden
      colour fg = fgi >= TRUE_COLOUR ? ca->truefg : win_get_colour(fgi);
      colour bg = bgi >= TRUE_COLOUR ? ca->truebg : win_get_colour(bgi);
      // separate ANSI values subject to BoldAsColour
      int fga = fgi >= ANSI0 ? fgi & 0xFF : 999;
      int bga = bgi >= ANSI0 ? bgi & 0xFF : 999;
      if ((ca->attr & ATTR_BOLD) && fga < 8 && term.enable_bold_colour && !rev) {
        if (bold_colour != (colour)-1)
          fg = bold_colour;
      }
      if (dim) {
        fg = ((fg & 0xFEFEFEFE) >> 1)
             // dim against terminal bg (as in apply_attr_colour)
             + ((win_get_colour(BG_COLOUR_I) & 0xFEFEFEFE) >> 1);
      }
      if (rev) {
        fgi ^= bgi; fga ^= bga; fg ^= bg;
        bgi ^= fgi; bga ^= fga; bg ^= fg;
        fgi ^= bgi; fga ^= bga; fg ^= bg;
      }
      cattr ac = apply_attr_colour(*ca, ACM_TERM);
      fg = ac.truefg;
      bg = ac.truebg;

      // add classes
      if (ca->attr & ATTR_BOLD)
        fprintf(hf, " bd");
      if (ca->attr & ATTR_ITALIC)
        fprintf(hf, " it");
      if ((ca->attr & (ATTR_UNDER | ATTR_STRIKEOUT)) == (ATTR_UNDER | ATTR_STRIKEOUT))
        fprintf(hf, " lu");
      else if (ca->attr & ATTR_STRIKEOUT)
        fprintf(hf, " st");
      else if (ca->attr & UNDER_MASK)
        fprintf(hf, " ul");
      int findex = (ca->attr & FONTFAM_MASK) >> ATTR_FONTFAM_SHIFT;
      if (findex)
        fprintf(hf, " font%d", findex);
      if (ca->attr & ATTR_FRAMED)
        fprintf(hf, " emoji");  // mark emoji style

      // catch and verify predefined colours and apply their colour classes
      if (fgi == FG_COLOUR_I) {
        if ((ca->attr & ATTR_BOLD) && term.enable_bold_colour) {
          if (fg == bold_colour) {
            fprintf(hf, " bold-color");
            fg = (colour)-1;
          }
        }
        else if (fg == fg_colour)
          fg = (colour)-1;
      }
      else if (fga < 8 && cfg.bold_as_colour && (ca->attr & ATTR_BOLD)
               && fg == win_get_colour(ANSI0 + fga + 8)
              )
      {
        fprintf(hf, " fg-color%d", fga + 8);
        fg = (colour)-1;
      }
      else if (fga < 16 && fg == win_get_colour(ANSI0 + fga)) {
        fprintf(hf, " fg-color%d", fga);
        fg = (colour)-1;
      }
      if (bgi == BG_COLOUR_I && bg == bg_colour)
        bg = (colour)-1;
      else if (bga < 16 && bg == win_get_colour(ANSI0 + bga)) {
        fprintf(hf, " bg-color%d", bga);
        bg = (colour)-1;
      }

#ifdef support_cursor
#warning this does not work because the TATTRs are not set in the buffer
      if (ca->attr & (TATTR_ACTCURS | TATTR_PASCURS))
        fprintf(hf, " cursor");
#endif

      // add styles
      bool with_style = false;
      void add_style(char * s) {
        if (!with_style) {
          fprintf(hf, "' style='%s", s);
          with_style = true;
        }
        else
          fprintf(hf, " %s", s);
      }

      // add individual colours, or fix unmatched colours
      if (fg != (colour)-1) {
        uchar r = red(fg), g = green(fg), b = blue(fg);
        add_style("");
        fprintf(hf, "color: #%02X%02X%02X;", r, g, b);
      }
      if (bg != (colour)-1) {
        uchar r = red(bg), g = green(bg), b = blue(bg);
        add_style("");
        fprintf(hf, "background-color: #%02X%02X%02X;", r, g, b);
      }

      if (ca->attr & ATTR_OVERL) {
        add_style("text-decoration-line: overline");
        if (ca->attr & ATTR_STRIKEOUT)
          fprintf(hf, " line-through");
        if (ca->attr & ATTR_UNDER)
          fprintf(hf, " underline");
        fprintf(hf, ";");
      }
      if (ca->attr & ATTR_BROKENUND)
        if (ca->attr & ATTR_DOUBLYUND)
          add_style("text-decoration-style: dashed;");
        else
          add_style("text-decoration-style: dotted;");
      else if ((ca->attr & UNDER_MASK) == ATTR_CURLYUND)
        add_style("text-decoration-style: wavy;");
      else if ((ca->attr & UNDER_MASK) == ATTR_DOUBLYUND)
        add_style("text-decoration-style: double;");

      colour ul = (ca->attr & ATTR_ULCOLOUR) ? ca->ulcolr : cfg.underl_colour;
      if (ul != (colour)-1 && (ca->attr & (UNDER_MASK | ATTR_STRIKEOUT | ATTR_OVERL))) {
        uchar r = red(ul), g = green(ul), b = blue(ul);
        add_style("");
        fprintf(hf, "text-decoration-color: #%02X%02X%02X;", r, g, b);
      }

      if (ca->attr & ATTR_INVISIBLE)
        add_style("visibility: hidden;");
      else {
        // add JavaScript triggers
        if (ca->attr & ATTR_BLINK2)
          fprintf(hf, "' name='rapid");
        else if (ca->attr & ATTR_BLINK)
          fprintf(hf, "' name='blink");
      }

      // write chunk
      fprintf(hf, "'>%s</span>", s);
      free(s);

      // forward chunk pointer
      i0 = i;
    }

    // forward newlines
    if (buf->text[i] == '\r') {
      i++;
      i0 = i;
    }
    if (buf->text[i] == '\n') {
      i++;
      i0 = i;
      fprintf(hf, "\n");
      odd = !odd;
    }
  }
  destroy_clip_workbuf(buf);

  fprintf(hf, "</pre>\n");
  fprintf(hf, "  </div>\n");
  fprintf(hf, "  </td></tr></table>\n");
  fprintf(hf, "</body>\n");

  fclose(hf);  // implies close(hfd);

  if (do_open) {
    wchar * browse = cs__mbstowcs(htmlf);
    win_open(browse);
    free(browse);
  }
  free(htmlf);
}

