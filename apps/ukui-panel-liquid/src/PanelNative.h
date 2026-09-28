#pragma once
class QWidget;
void setPanelNativeBackdrop(QWidget *panel, bool liquid);

class QImage;
void setPanelNativeOutline(QWidget *panel, const QImage &coverage);
