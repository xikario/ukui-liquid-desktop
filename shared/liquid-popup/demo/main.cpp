#include "LiquidPopup.h"
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QTimer>
#include <QDir>
using namespace LiquidPopup;
int main(int argc,char **argv) {
    QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QApplication app(argc,argv);install(app);
    QWidget window;window.setWindowTitle("公共液态气泡 · 四程序预览");window.resize(860,520);
    window.setStyleSheet("QWidget#preview {background: qlineargradient(x1:0,y1:0,x2:1,y2:1,stop:0 #243d58,stop:0.45 #495278,stop:1 #977969);color:white;} QPushButton {background:rgba(255,255,255,25); color:white; padding:16px; border:1px solid #8994aa; border-radius:14px;} QLabel {color:white;}");window.setObjectName("preview");
    auto *layout=new QVBoxLayout(&window);
    auto *title=new QLabel("一份公共实现，四个程序共用");title->setStyleSheet("font-size:26px; font-weight:600;");layout->addWidget(title);
    layout->addWidget(new QLabel("仅预览效果，不修改应用设置 · 悬停看气泡，点击看菜单"));
    for(const QString &name:QStringList{"开始菜单","智能空间","ukui-fences","系统监视小组件"}) {
        auto *b=new QPushButton(name);b->setToolTip(name+"\n液态材质 · 圆润连接颈 · 锚点展开");layout->addWidget(b);
        QObject::connect(b,&QPushButton::clicked,&window,[b,name]{
            QMenu m;m.setStyleSheet("QMenu{color:white;padding:8px;} QMenu::item{padding:8px 24px;border-radius:7px;} QMenu::item:selected{background:#49648d;} QMenu::separator{height:1px;background:#718099;}");
            m.addAction(name);m.addSeparator();auto *a=m.addAction("勾选状态示例");a->setCheckable(true);a->setChecked(true);
            auto *sub=m.addMenu("子菜单示例");sub->addAction("示例选项一");sub->addAction("示例选项二");
            m.addAction("暂不可用")->setEnabled(false);m.addAction("关闭");execAt(m,b);
        });
    }
    window.show();
    if(app.arguments().contains("--snapshot")) {
        theme().reducedMotion=true;
        QTimer::singleShot(150,&window,[&]{
            showText(window.mapToGlobal(QPoint(390,140)),"系统监视\nCPU 24% · 内存 42%\n统一的液态提示气泡",&window);
            QTimer::singleShot(100,&window,[&]{
                QDir().mkpath("artifacts");window.grab().save("artifacts/preview.png");
                for(auto *w:QApplication::topLevelWidgets())if(w->objectName()=="liquidPopupShell")w->grab().save("artifacts/bubble.png");
                app.quit();
            });
        });
    }
    return app.exec();
}
