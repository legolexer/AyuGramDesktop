#include "wg_widgets.h"

#include <QCheckBox>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDoubleSpinBox>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <cmath>

namespace wg {

QString formatNumber(int64_t n) {
	QString s = QString::number(n), out;
	for (int i = 0; i < s.size(); i++) {
		if (i && (s.size() - i) % 3 == 0) out += QChar(0x00A0);
		out += s[i];
	}
	return out;
}

static constexpr double kPi = 3.14159265358979323846;   // без M_PI: на MSVC он не всегда определён

static void drawStar(QPainter &p, QPointF c, qreal r, const QColor &fill) {
	QPolygonF poly;
	for (int i = 0; i < 10; i++) {
		const qreal a = -kPi / 2 + i * kPi / 5, rr = (i % 2) ? r * 0.45 : r;
		poly << QPointF(c.x() + rr * std::cos(a), c.y() + rr * std::sin(a));
	}
	p.setPen(Qt::NoPen);
	p.setBrush(fill);
	p.drawPolygon(poly);
}

// ---------------- GiftCard ----------------
GiftCard::GiftCard(const FeedItem &item, QWidget *parent) : QWidget(parent), _item(item) {
	setFixedSize(138, 128);
	setCursor(Qt::PointingHandCursor);
	setMouseTracking(true);
}

void GiftCard::paintEvent(QPaintEvent *) {
	QPainter p(this);
	p.setRenderHint(QPainter::Antialiasing);
	const QRectF r = QRectF(rect()).adjusted(1, 1, -1, -1);
	QRadialGradient g(r.center().x(), r.top() + r.height() * 0.4, r.width() * 0.85);
	g.setColorAt(0, QColor::fromRgba(_item.backdropCenter));
	g.setColorAt(1, QColor::fromRgba(_item.backdropEdge));
	p.setPen(Qt::NoPen);
	p.setBrush(g);
	p.drawRoundedRect(r, 12, 12);

	const QRectF m(width() / 2.0 - 28, 8, 56, 56);
	if (!_model.isNull()) {
		p.drawPixmap(m.toRect(), _model);
	} else {
		p.setBrush(QColor(255, 255, 255, 50));
		p.drawEllipse(m);
		drawStar(p, m.center(), 20, Qt::white);
	}

	QFont f = font();
	f.setPixelSize(12);
	p.setFont(f);
	p.setPen(Qt::white);
	const QString seller = _item.sellerUsername.empty() ? QString("id %1").arg(_item.sellerId)
		: "@" + QString::fromStdString(_item.sellerUsername);
	p.drawText(QRectF(4, 68, width() - 8, 16), Qt::AlignCenter,
		QFontMetrics(f).elidedText(seller, Qt::ElideRight, width() - 8));

	// цена: число и звезда (у апгрейдов цены нет)
	f.setBold(true);
	p.setFont(f);
	const QColor gold(0xf5, 0xc5, 0x42);
	p.setPen(gold);
	if (_item.priceStars) {
		const QString price = formatNumber(*_item.priceStars);
		const int tw = QFontMetrics(f).horizontalAdvance(price);
		const qreal x0 = (width() - (tw + 16)) / 2.0;
		p.drawText(QRectF(x0, 85, tw, 16), Qt::AlignLeft | Qt::AlignVCenter, price);
		drawStar(p, QPointF(x0 + tw + 10, 93), 6.5, gold);
	} else {
		p.drawText(QRectF(4, 85, width() - 8, 16), Qt::AlignCenter, QString::fromUtf8("—"));
	}

	f.setBold(false);
	f.setPixelSize(11);
	p.setFont(f);
	p.setPen(QColor(255, 255, 255, 190));
	p.drawText(QRectF(2, 104, width() - 4, 16), Qt::AlignCenter, QString::fromStdString(caption(_item)));

	if (!_item.seen) {
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(QColor(0x3f, 0xcf, 0x6a), 2));
		p.drawRoundedRect(r.adjusted(1, 1, -1, -1), 11, 11);
	}
	if (_hover) {
		p.setPen(Qt::NoPen);
		p.setBrush(QColor(255, 255, 255, 25));
		p.drawRoundedRect(r, 12, 12);
	}
}

void GiftCard::mousePressEvent(QMouseEvent *e) {
	if (e->button() == Qt::LeftButton) {
		_item.seen = true;
		update();
		emit clicked(_item);
	}
}

void GiftCard::contextMenuEvent(QContextMenuEvent *e) {
	QMenu menu(this);
	QMenu *sub = menu.addMenu(QString::fromUtf8("В чёрный список"));
	const QString who = _item.sellerUsername.empty() ? QString::fromUtf8("продавца")
		: QString::fromUtf8("продавца @") + QString::fromStdString(_item.sellerUsername);
	QAction *a1 = sub->addAction(who);
	QAction *a2 = sub->addAction(QString::fromUtf8("коллекцию ") + QString::fromStdString(_item.title));
	QAction *a3 = sub->addAction(QString::fromUtf8("этот подарок"));
	QAction *chosen = menu.exec(e->globalPos());
	if (chosen == a1) emit blacklistRequested(int(BlackType::Seller), _item);
	else if (chosen == a2) emit blacklistRequested(int(BlackType::Collection), _item);
	else if (chosen == a3) emit blacklistRequested(int(BlackType::Item), _item);
}

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
void GiftCard::enterEvent(QEnterEvent *) { _hover = true; update(); }
#else
void GiftCard::enterEvent(QEvent *) { _hover = true; update(); }
#endif
void GiftCard::leaveEvent(QEvent *) { _hover = false; update(); }

// ---------------- GiftStrip ----------------
GiftStrip::GiftStrip(QWidget *parent) : QWidget(parent) {
	setFixedHeight(168);
	auto *outer = new QVBoxLayout(this);
	outer->setContentsMargins(12, 6, 12, 10);
	outer->setSpacing(4);
	auto *hint = new QLabel(QString::fromUtf8("F9 настройки"), this);
	hint->setAlignment(Qt::AlignRight);
	hint->setStyleSheet("color:#7f91a4;font-size:12px;");
	outer->addWidget(hint);

	_scroll = new QScrollArea(this);
	_scroll->setFrameShape(QFrame::NoFrame);
	_scroll->setWidgetResizable(true);
	_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	_scroll->setStyleSheet("background:transparent;");
	_scroll->viewport()->installEventFilter(this);
	_row = new QWidget;
	_row->setStyleSheet("background:transparent;");
	_lay = new QHBoxLayout(_row);
	_lay->setContentsMargins(2, 2, 2, 2);
	_lay->setSpacing(10);
	_lay->addStretch(1);
	_scroll->setWidget(_row);
	outer->addWidget(_scroll);
}

void GiftStrip::setBackgroundColor(const QColor &c) { _bg = c; update(); }

void GiftStrip::paintEvent(QPaintEvent *) { QPainter(this).fillRect(rect(), _bg); }

void GiftStrip::setItems(const std::deque<FeedItem> &items) {
	while (_lay->count() > 1) {
		QLayoutItem *it = _lay->takeAt(0);
		if (it->widget()) it->widget()->deleteLater();
		delete it;
	}
	int pos = 0;
	for (const auto &item : items) {
		auto *card = new GiftCard(item, _row);
		connect(card, &GiftCard::clicked, this, &GiftStrip::itemClicked);
		connect(card, &GiftCard::blacklistRequested, this, [this](int type, const FeedItem &it) {
			if (confirmBlacklist(type, it)) emit blacklistRequested(type, it);   // без подтверждения ничего не добавляем
		});
		_lay->insertWidget(pos++, card);
	}
}

bool GiftStrip::confirmBlacklist(int type, const FeedItem &i) {
	const QString title = QString::fromStdString(i.title);
	QString text;
	if (type == int(BlackType::Seller)) {
		const QString who = i.sellerUsername.empty() ? QString("id %1").arg(i.sellerId)
			: "@" + QString::fromStdString(i.sellerUsername);
		text = QString::fromUtf8("Точно добавить продавца %1 в чёрный список?\nЕго лоты перестанут показываться в ленте.").arg(who);
	} else if (type == int(BlackType::Collection)) {
		text = QString::fromUtf8("Точно добавить коллекцию «%1» в чёрный список?\nПодарки этой коллекции перестанут показываться в ленте.").arg(title);
	} else {
		text = QString::fromUtf8("Точно добавить этот подарок (%1 #%2) в чёрный список?").arg(title).arg(i.num);
	}
	if (_confirm) return _confirm(text);
	QMessageBox box(QMessageBox::Question, QString::fromUtf8("Чёрный список"), text, QMessageBox::NoButton, this);
	QPushButton *yes = box.addButton(QString::fromUtf8("Добавить"), QMessageBox::AcceptRole);
	QPushButton *cancel = box.addButton(QString::fromUtf8("Отмена"), QMessageBox::RejectRole);
	box.setDefaultButton(cancel);
	box.exec();
	return box.clickedButton() == yes;
}

bool GiftStrip::eventFilter(QObject *o, QEvent *e) {
	if (o == _scroll->viewport() && e->type() == QEvent::Wheel) {
		auto *w = static_cast<QWheelEvent *>(e);
		const int d = w->angleDelta().y() ? w->angleDelta().y() : w->angleDelta().x();
		_scroll->horizontalScrollBar()->setValue(_scroll->horizontalScrollBar()->value() - d);
		return true;
	}
	return QWidget::eventFilter(o, e);
}

} // namespace wg
