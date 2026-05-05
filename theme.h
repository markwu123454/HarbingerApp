#pragma once
#include <QGuiApplication>
#include <QStyleHints>
#include <QColor>
#include <QString>

// All colors are derived from the system color scheme (light/dark).
// Call Theme::isDark() or any color helper freely from paintEvent — they are
// cheap inline reads of a Qt-managed value.
namespace Theme {

inline bool isDark() {
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

// ── Container backgrounds ────────────────────────────────────────
inline QColor topbarBg()  { return isDark() ? QColor(0x0d, 0x11, 0x17) : QColor(0x1a, 0x23, 0x32); }
inline QColor windowBg()  { return isDark() ? QColor(0x1e, 0x21, 0x28) : QColor(0xf3, 0xf4, 0xf6); }
inline QColor surfaceBg() { return isDark() ? QColor(0x27, 0x2b, 0x33) : QColor(0xff, 0xff, 0xff); }
inline QColor stripBg()   { return isDark() ? QColor(0x16, 0x19, 0x1f) : QColor(0xf9, 0xfa, 0xfb); }
inline QColor widgetBg()  { return isDark() ? QColor(0x22, 0x26, 0x2e) : QColor(0xf8, 0xf9, 0xfa); }
inline QColor aimBg()     { return isDark() ? QColor(0x0d, 0x11, 0x17) : QColor(0x22, 0x2c, 0x3e); }

// ── Borders ───────────────────────────────────────────────────────
inline QColor border()      { return isDark() ? QColor(0x3a, 0x40, 0x4c) : QColor(0xd1, 0xd5, 0xdb); }
inline QColor borderLight() { return isDark() ? QColor(0x2a, 0x2e, 0x38) : QColor(0xe5, 0xe7, 0xeb); }
inline QColor tickMajor()   { return isDark() ? QColor(0x6b, 0x72, 0x80) : QColor(0xad, 0xb5, 0xbd); }
inline QColor tickMinor()   { return isDark() ? QColor(0x3a, 0x40, 0x4c) : QColor(0xd0, 0xd4, 0xda); }
inline QColor trackBg()     { return isDark() ? QColor(0x3a, 0x40, 0x4c) : QColor(0xe5, 0xe7, 0xeb); }

// ── Text ──────────────────────────────────────────────────────────
inline QColor textPrimary()   { return isDark() ? QColor(0xe5, 0xe7, 0xeb) : QColor(0x1a, 0x23, 0x32); }
inline QColor textSecondary() { return isDark() ? QColor(0x9c, 0xa3, 0xaf) : QColor(0x6b, 0x72, 0x80); }
inline QColor textMuted()     { return isDark() ? QColor(0x6b, 0x72, 0x80) : QColor(0x9c, 0xa3, 0xaf); }
inline QColor centerDot()     { return isDark() ? QColor(0xe5, 0xe7, 0xeb) : QColor(0x1a, 0x23, 0x32); }

// ── AimWidget-specific ───────────────────────────────────────────
inline QColor gridLine()       { return QColor(255, 255, 255, isDark() ? 18 : 10); }
inline QColor centerLine()     { return QColor(255, 255, 255, isDark() ? 35 : 20); }
inline QColor aimOverlayText() { return QColor(255, 255, 255, isDark() ? 110 : 150); }
inline QColor aimHintText()    { return QColor(255, 255, 255, isDark() ? 40 : 60); }

// ── Application-wide Qt stylesheet ───────────────────────────────
inline QString stylesheet() {
    auto s = [](QColor c) -> QString { return c.name(); };
    bool dark = isDark();

    QColor btnBg      = dark ? QColor(0x30, 0x35, 0x40) : QColor(0xf3, 0xf4, 0xf6);
    QColor btnHover   = dark ? QColor(0x3a, 0x40, 0x4c) : QColor(0xe5, 0xe7, 0xeb);
    QColor btnPress   = dark ? QColor(0x44, 0x4b, 0x58) : QColor(0xd1, 0xd5, 0xdb);
    QColor btnDisBg   = dark ? QColor(0x22, 0x26, 0x2e) : QColor(0xf9, 0xfa, 0xfb);
    QColor btnDisText = dark ? QColor(0x4b, 0x55, 0x63) : QColor(0x9c, 0xa3, 0xaf);
    QColor btnDisBord = dark ? QColor(0x2a, 0x2e, 0x38) : QColor(0xe5, 0xe7, 0xeb);
    QColor listSel    = dark ? QColor(0x1e, 0x3a, 0x5f) : QColor(0xdb, 0xea, 0xfe);
    QColor listSelTxt = dark ? QColor(0x93, 0xc5, 0xfd) : QColor(0x1d, 0x4e, 0xd8);
    QColor grooveBg   = border();
    QColor text       = textPrimary();

    QString ss;
    ss += "QWidget { font-family: 'Segoe UI', sans-serif; font-size: 13px; color: " + s(text) + "; }\n";
    ss += "QPushButton { background: " + s(btnBg) + "; border: 1px solid " + s(grooveBg) + "; border-radius: 4px; padding: 5px 12px; }\n";
    ss += "QPushButton:hover { background: " + s(btnHover) + "; }\n";
    ss += "QPushButton:pressed { background: " + s(btnPress) + "; }\n";
    ss += "QPushButton:disabled { color: " + s(btnDisText) + "; border-color: " + s(btnDisBord) + "; background: " + s(btnDisBg) + "; }\n";
    ss += "QPushButton#masterArmBtn:checked { background: #f59e0b; color: white; border-color: #d97706; }\n";
    ss += "QPushButton#turretArmBtn:checked { background: #3b82f6; color: white; border-color: #2563eb; }\n";
    ss += "QPushButton#gunArmBtn:checked    { background: #ef4444; color: white; border-color: #dc2626; }\n";
    ss += "QSlider::groove:horizontal { height: 4px; background: " + s(grooveBg) + "; border-radius: 2px; }\n";
    ss += "QSlider::sub-page:horizontal { background: #3b82f6; border-radius: 2px; }\n";
    ss += "QSlider::handle:horizontal { width: 14px; height: 14px; background: #3b82f6; border-radius: 7px; margin: -5px 0; }\n";
    ss += "QSlider::handle:horizontal:disabled { background: " + s(grooveBg) + "; }\n";
    ss += "QListWidget { background: " + s(surfaceBg()) + "; }\n";
    ss += "QListWidget::item:selected { background: " + s(listSel) + "; color: " + s(listSelTxt) + "; }\n";
    ss += "QScrollBar:vertical { width: 6px; background: transparent; }\n";
    ss += "QScrollBar::handle:vertical { background: " + s(grooveBg) + "; border-radius: 3px; min-height: 20px; }\n";
    ss += "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }\n";
    ss += "QLabel#sectionLabel { color: " + s(textSecondary()) + "; font-size: 10px; font-weight: 700; letter-spacing: 1px; }\n";
    ss += "QLabel#telLabel { color: " + s(textMuted()) + "; font-size: 9px; font-weight: 700; letter-spacing: 1px; background: transparent; border: none; }\n";

    return ss;
}

} // namespace Theme
