#ifndef CBAR_H
#define CBAR_H

#define FONT_SET_TYPE(N) const char* N[]
#define FONT_SET_LENGTH(N) (sizeof(N) / sizeof(const char*))
#define FONT_SET_GET(N, I) (N[I])

#define CFG_GEOMETRY NULL
#define CFG_WM_NAME "EXWM"
#define CFG_IS_ON_BOTTOM false
#define CFG_FORCE_DOCKING false
#define CFG_FONTS { [0] = "DejaVu Sans Mono:size=11:hintstyle=hintfull:antialias=true:hinting=true" }
#define CFG_AREAS 0
#define CFG_UNDERLINE_HEIGHT 2
#define CFG_VOFFSET 0
/* default colors */
#define CFG_BGCOLOR "#242424"
#define CFG_FGCOLOR "#bfbfbf"
#define CFG_UCOLOR  "#7f7f7f"

#define RENDER_FUNC int render(void)

typedef union rgba_t {
    struct {
        uint8_t b;
        uint8_t g;
        uint8_t r;
        uint8_t a;
    };
    uint32_t v;
} rgba_t;

/* R - Swap the current background and foreground colors. */
void bar_invcolors(void);
/* l - aligns the following text to the left side of the screen */
void bar_leftalign(void);
/* c - aligns the following text to the center of the screen */
void bar_centeralign(void);
/* r - aligns the following text to the right of the screen */
void bar_rightalign(void);
/* Owidth - offsets the current position by width pixels in the alignment direction */
void bar_offset(int offset);
/* Bcolor, Fcolor, Ucolor - sets the text, bg, and underline colors */
void bar_bgcolor (rgba_t color);
void bar_fgcolor (rgba_t color);
void bar_ucolor  (rgba_t color);
/* link color format character to rgba_t type */

enum bar_cflag {
    BAR_FG, BAR_BG, BAR_U
};
void bar_lncolor(char c, enum bar_cflag flag, rgba_t color);
void bar_colorsegment(const char* str);
/* creates a rgba_t from a string */
rgba_t bar_mkcolor(const char* str);
/* Tindex - Set the font used to draw the following text, 1-based. */
void bar_font(int idx);
/* Sdir - Set the monitor being rendered to, 0-based. */
void bar_monitor(int idx);
/* [+,-,!]attribute - Set attribute flags. */
#define BAR_NONE      0x0
#define BAR_OVERLINE  0x1
#define BAR_UNDERLINE 0x2
void bar_attribute(uint32_t flags);

/* append text */
void bar_puts(const char* c);
void bar_putsn(const char* c, size_t amt);
void bar_printf(const char* c, ...);
void bar_vprintf(const char* c, va_list args);

#endif
