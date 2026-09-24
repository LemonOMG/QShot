// Probe: replicate the REAL sequence with the REAL ToolbarWidget, whose sub-panel
// is an UNOWNED top-level window (QWidget(nullptr)) -- unlike the parented panels
// used in the earlier z-order probe. Checks whether the sub-panel survives the
// overlay->activateWindow() that showToolbar()/handleToolSelection() now perform.
#include <QApplication>
#include <QWidget>
#include <QWindow>
#include <QThread>
#include <cstdio>
#include <windows.h>
#include "overlay/ToolbarWidget.h"

static void settle(int ms = 250) {
    for (int i = 0; i < ms / 20; ++i) {
        QCoreApplication::processEvents();
        QThread::msleep(20);
    }
}

// GW_HWNDPREV walks UP the z-order, so finding b while walking up from a means b
// is drawn above a.
static const char* rel(QWidget* a, QWidget* b) {
    if (!a || !b || !a->isVisible() || !b->isVisible()) return "not visible";
    HWND ha = reinterpret_cast<HWND>(a->winId());
    HWND hb = reinterpret_cast<HWND>(b->winId());
    for (HWND h = ha; h; h = GetWindow(h, GW_HWNDPREV)) if (h == hb) return "sub ABOVE overlay (good)";
    for (HWND h = hb; h; h = GetWindow(h, GW_HWNDPREV)) if (h == ha) return "sub BELOW overlay (BAD)";
    return "indeterminate";
}

static QWidget* findSubPanel(QWidget* toolbar) {
    for (QWidget* w : QApplication::topLevelWidgets()) {
        if (w != toolbar && w->isVisible() && w->parentWidget() == nullptr) return w;
    }
    return nullptr;
}

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    using namespace qshot;

    QWidget* overlay = new QWidget;
    overlay->setWindowFlags(Qt::Window | Qt::FramelessWindowHint
                            | Qt::WindowStaysOnTopHint | Qt::Tool);
    overlay->resize(900, 600);
    overlay->show();
    overlay->activateWindow();
    settle();

    ToolbarWidget* toolbar = new ToolbarWidget(overlay);
    toolbar->resize(564, 40);
    toolbar->move(200, 400);
    toolbar->show();
    overlay->activateWindow();          // reclaimKeyboardFocus() after showToolbar()
    settle();
    printf("[1] showToolbar()                 sub=%s\n",
           findSubPanel(toolbar) ? "visible" : "none");

    toolbar->handleToolClick(AnnotationType::Text);   // user picks the Text tool
    settle();
    QWidget* sub = findSubPanel(toolbar);
    printf("[2] Text tool selected            sub=%s   %s\n",
           sub ? "visible" : "none", sub ? rel(overlay, sub) : "-");

    overlay->activateWindow();          // reclaimKeyboardFocus() in handleToolSelection()
    settle();
    printf("[3] + overlay->activateWindow()   sub=%s   %s\n",
           sub ? "visible" : "none", sub ? rel(overlay, sub) : "-");

    toolbar->handleToolClick(AnnotationType::Text);   // toggle off
    settle();
    toolbar->handleToolClick(AnnotationType::Mosaic);
    settle();
    sub = findSubPanel(toolbar);
    printf("[4] Mosaic tool selected          sub=%s   %s\n",
           sub ? "visible" : "none", sub ? rel(overlay, sub) : "-");
    overlay->activateWindow();
    settle();
    printf("[5] + overlay->activateWindow()   sub=%s   %s\n",
           sub ? "visible" : "none", sub ? rel(overlay, sub) : "-");

    return 0;
}
