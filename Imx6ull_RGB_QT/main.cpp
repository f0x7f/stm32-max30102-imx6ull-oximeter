#include <QApplication>
#include <QLocalSocket>
#include <QWidget>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <QVector>
#include <QPolygonF>
#include <QPainter>
#include <QPen>
#include <QtGlobal>

/* ---- 滚动波形控件：把最近400个IR样本画成折线 ---- */
class Waveform : public QWidget {
public:
    QVector<double> data;
    void push(double v) {
        data.push_back(v);
        while (data.size() > 400) data.removeFirst();
        update();
    }
protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.fillRect(rect(), QColor(8, 14, 20));
        if (data.size() < 2) return;
        double maxv = -1e18, minv = 1e18;
        for (double v : data) { maxv = qMax(maxv, v); minv = qMin(minv, v); }
        double span = (maxv - minv); if (span < 1) span = 1;
        double xstep = (double)width() / (data.size() - 1);
        QPolygonF poly;
        for (int i = 0; i < data.size(); i++) {
            double x = i * xstep;
            double y = height() - 10 - (data[i] - minv) / span * (height() - 20);
            poly << QPointF(x, y);
        }
        p.setPen(QPen(QColor(0, 255, 100), 1.5));
        p.drawPolyline(poly);
    }
};

int main(int argc, char **argv) {
    QApplication app(argc, argv);

    QWidget win;
    win.setWindowTitle("Heart Rate Monitor");
//    win.resize(1024, 600);


    QVBoxLayout *v = new QVBoxLayout(&win);
    QHBoxLayout *h = new QHBoxLayout;
    QLabel *hr  = new QLabel("HR: --");      hr->setStyleSheet("font-size:56px;color:#ff4444;");
    QLabel *sp  = new QLabel("SpO2: --");    sp->setStyleSheet("font-size:56px;color:#44ff44;");
    QLabel *stt = new QLabel("disconnected");
    h->addWidget(hr); h->addWidget(sp); h->addStretch(); h->addWidget(stt);
    v->addLayout(h);
    Waveform *wf = new Waveform;
    wf->setMinimumHeight(350);
    v->addWidget(wf);

    QLocalSocket sock;
    QObject::connect(&sock, &QLocalSocket::connected, [&]{ stt->setText("connected"); });
    QObject::connect(&sock, &QLocalSocket::disconnected, [&]{ stt->setText("disconnected"); });
    QObject::connect(&sock, &QLocalSocket::readyRead, [&]{
        while (sock.canReadLine()) {
            const QList<QByteArray> f = sock.readLine().trimmed().split(' ');
            if (f.size() < 2) continue;
            if (f[0] == "S" && f.size() >= 3) {
                wf->push(f[1].toDouble());          /* 画IR波形 */
            } else if (f[0] == "R" && f.size() >= 5) {
                int hrv = f[2].toInt(), spv = f[4].toInt();
                hr->setText(hrv ? QString("HR: %1").arg(f[1].toInt())
                                : QString("HR: --"));
                sp->setText(spv ? QString("SpO2: %1%").arg(f[3].toInt())
                                : QString("SpO2: --"));
            }
        }
    });

    /* 自动重连：每1秒检查一次 */
    QTimer *re = new QTimer(&win);
    re->setInterval(1000);
    QObject::connect(re, &QTimer::timeout, [&]{
        if (sock.state() != QLocalSocket::ConnectedState)
            sock.connectToServer("/tmp/max30102.sock");
    });
    re->start();

    win.showFullScreen();
//    win.show();
    sock.connectToServer("/tmp/max30102.sock");
    return app.exec();
}
