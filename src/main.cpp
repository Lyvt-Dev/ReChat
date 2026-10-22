#include <QtWidgets>
#include <QtNetwork>
#include <functional>
#include <memory>

static constexpr quint16 DiscoveryPort = 45454;
static const QHostAddress Group("239.255.42.99");
static bool validName(const QString &s) { return QRegularExpression("^[a-z][a-z0-9_]{2,23}$").match(s).hasMatch(); }
struct Peer { QString id, name; QHostAddress address; quint16 port; qint64 seen; };
class Network : public QObject {
public:
    QString id = QUuid::createUuid().toString(QUuid::WithoutBraces), name;
    QMap<QString,Peer> peers;
    QUdpSocket udp; QTcpServer server; QTimer heartbeat;
    std::function<void()> changed;
    std::function<void(QString,QString)> message;
    std::function<void(QString)> conflict;
    bool start() {
        if (!server.listen(QHostAddress::AnyIPv4, 0)) return false;
        if (!udp.bind(QHostAddress::AnyIPv4, DiscoveryPort, QUdpSocket::ShareAddress | QUdpSocket::ReuseAddressHint)) return false;
        bool joined=false;
        for (const auto &iface : QNetworkInterface::allInterfaces())
            if (iface.flags().testFlag(QNetworkInterface::IsUp) && iface.flags().testFlag(QNetworkInterface::CanMulticast)) joined=udp.joinMulticastGroup(Group,iface)||joined;
        connect(&udp,&QUdpSocket::readyRead,this,[this]{
            while(udp.hasPendingDatagrams()) {
                auto d=udp.receiveDatagram(2048);
                auto o=QJsonDocument::fromJson(d.data()).object();
                QString other=o["id"].toString(), n=o["name"].toString(); int port=o["port"].toInt();
                if(o["app"]!="ReChat/1" || other==id || QUuid(other).isNull() || !validName(n) || port<1 || port>65535) continue;
                peers[other]={other,n,d.senderAddress(),quint16(port),QDateTime::currentMSecsSinceEpoch()};
                if(n==name && other<id) { name.clear(); if(conflict) conflict(n); }
                if(changed) changed();
            }
        });
        connect(&server,&QTcpServer::newConnection,this,[this]{
            while(server.hasPendingConnections()) {
                auto s=server.nextPendingConnection(); s->setReadBufferSize(32768);
                auto buffer=std::make_shared<QByteArray>();
                connect(s,&QTcpSocket::readyRead,s,[this,s,buffer]{
                    *buffer+=s->readAll();
                    if(buffer->size()>24000) { s->abort(); return; }
                    if(!buffer->contains('\n')) return;
                    auto o=QJsonDocument::fromJson(buffer->left(buffer->indexOf('\n'))).object();
                    const QString from=o["from"].toString(), text=o["text"].toString();
                    if(o["app"]=="ReChat/1" && o["to"].toString()==id && peers.contains(from) && peers[from].address==s->peerAddress() && !text.isEmpty() && text.size()<=4000 && !name.isEmpty()) {
                        if(message) message(from,text);
                        s->write("OK\n"); s->disconnectFromHost();
                    } else s->abort();
                });
                connect(s,&QTcpSocket::disconnected,s,&QObject::deleteLater);
                QTimer::singleShot(5000,s,[s]{s->abort();s->deleteLater();});
            }
        });
        connect(&heartbeat,&QTimer::timeout,this,[this]{
            auto now=QDateTime::currentMSecsSinceEpoch(); bool removed=false;
            for(auto i=peers.begin();i!=peers.end();) if(now-i->seen>9000) {i=peers.erase(i);removed=true;} else ++i;
            if(removed&&changed) changed(); announce();
        }); heartbeat.start(2000); announce(); return joined;
    }
    void announce() {
        if(name.isEmpty()) return;
        QByteArray data=QJsonDocument(QJsonObject{{"app","ReChat/1"},{"id",id},{"name",name},{"port",server.serverPort()}}).toJson(QJsonDocument::Compact);
        for(const auto &iface:QNetworkInterface::allInterfaces()) if(iface.flags().testFlag(QNetworkInterface::IsUp)&&iface.flags().testFlag(QNetworkInterface::CanMulticast)) {udp.setMulticastInterface(iface);udp.writeDatagram(data,Group,DiscoveryPort);}
    }
    void send(const QString &to,const QString &text,std::function<void(bool)> done) {
        if(!peers.contains(to)||name.isEmpty()) {done(false);return;}
        auto s=new QTcpSocket(this); auto completed=std::make_shared<bool>(false); auto response=std::make_shared<QByteArray>();
        auto finish=[s,completed,done](bool ok){if(*completed)return;*completed=true;done(ok);s->abort();s->deleteLater();};
        connect(s,&QTcpSocket::connected,s,[this,s,to,text]{s->write(QJsonDocument(QJsonObject{{"app","ReChat/1"},{"from",id},{"to",to},{"text",text}}).toJson(QJsonDocument::Compact)+'\n');});
        connect(s,&QTcpSocket::readyRead,s,[s,response,finish]{*response+=s->readAll();if(response->contains('\n'))finish(*response=="OK\n");});
        connect(s,&QTcpSocket::errorOccurred,s,[finish](QAbstractSocket::SocketError){finish(false);});
        QTimer::singleShot(5000,s,[finish]{finish(false);});
        s->connectToHost(peers[to].address,peers[to].port);
    }
};

class Window : public QMainWindow {
    Network net; QSettings settings; QListWidget *list; QTextBrowser *chat; QLineEdit *input,*search; QLabel *title,*subtitle,*profile,*status; QPushButton *sendButton;
    QString active; QJsonArray history; QString historyPath;
    void save() {
        QSaveFile f(historyPath); if(f.open(QIODevice::WriteOnly)) {f.write(QJsonDocument(history).toJson());if(f.commit())return;}
        status->setText("Chatverlauf konnte nicht gespeichert werden.");
    }
    void add(const QString &peer,const QString &text,bool own) {
        history.append(QJsonObject{{"peer",peer},{"text",text},{"own",own},{"time",QDateTime::currentDateTime().toString("HH:mm")}});
        save(); if(active==peer) render();
    }
    void render() {
        chat->clear();
        if(active.isEmpty()) { chat->setHtml("<br><br><br><center><h1>Dein Netzwerk. Deine Gespräche.</h1><p style='color:#a5a2b8'>Wähle jemanden aus deinem LAN.<br>Kein Account. Keine Cloud. Einfach ReChat.</p></center>"); return; }
        QString html;
        for(const auto &v:history) {auto o=v.toObject();if(o["peer"].toString()!=active)continue;
            bool own=o["own"].toBool();
            html+=QString("<table width='100%' cellspacing='8'><tr><td align='%1'><table bgcolor='%2' cellpadding='14'><tr><td><font color='#f4f2ff'>%3</font><br><font size='2' color='#bab2d9'>%4 · %5</font></td></tr></table></td></tr></table>")
                .arg(own?"right":"left",own?"#5d49b8":"#252734",o["text"].toString().toHtmlEscaped().replace("\n","<br>"),o["time"].toString(),own?"Du":"Empfangen");
        }
        chat->setHtml(html);chat->verticalScrollBar()->setValue(chat->verticalScrollBar()->maximum());
    }
    void refresh() {
        QString selected=active;list->clear();
        for(const auto &p:net.peers) {
            if(!p.name.contains(search->text(),Qt::CaseInsensitive))continue;
            auto item=new QListWidgetItem("●   "+p.name+"\n     @"+p.name+"  ·  Im Netzwerk",list);item->setData(Qt::UserRole,p.id);item->setSizeHint(QSize(240,76));
            if(p.id==selected)list->setCurrentItem(item);
        }
        status->setText(QString("●  Lokal verbunden · %1 online").arg(net.peers.size()));
        bool available=net.peers.contains(active)&&!net.name.isEmpty();sendButton->setEnabled(available);input->setEnabled(available);
        if(!active.isEmpty())subtitle->setText(available?"● Online · Direkt im lokalen Netzwerk":"Offline · Nachrichten bleiben auf diesem Gerät");
    }
    void claim() {
        bool ok=false;QString n=QInputDialog::getText(this,"Dein ReChat-Username","3–24 Zeichen: a–z, 0–9, _; beginnt mit Buchstabe.\nNur im erreichbaren LAN reserviert, solange du online bist.",QLineEdit::Normal,net.name,&ok).trimmed().toLower();
        if(!ok)return;
        if(!validName(n)) {QMessageBox::information(this,"Ungültiger Username","Bitte verwende das angegebene Format.");return;}
        for(const auto &p:net.peers) if(p.name==n) {QMessageBox::information(this,"Schon vergeben","Dieser Username ist gerade im LAN belegt.");return;}
        net.name=n;settings.setValue("username",n);profile->setText("@"+n+"\nDein lokales Profil");net.announce();refresh();
    }
public:
    Window() {
        setWindowTitle("ReChat — Gemeinsam. Lokal.");setWindowIcon(QIcon(":/assets/logo.svg"));resize(1180,780);setMinimumSize(820,580);
        net.id=settings.value("device",net.id).toString();settings.setValue("device",net.id);net.name=settings.value("username").toString();
        QString path=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);QDir().mkpath(path);historyPath=path+"/history.json";
        QFile f(historyPath);if(f.open(QIODevice::ReadOnly))history=QJsonDocument::fromJson(f.readAll()).array();
        auto root=new QWidget;setCentralWidget(root);auto layout=new QHBoxLayout(root);layout->setContentsMargins(0,0,0,0);layout->setSpacing(0);
        auto rail=new QFrame;rail->setObjectName("rail");rail->setFixedWidth(78);auto r=new QVBoxLayout(rail);r->setContentsMargins(13,24,13,20);
        auto logo=new QLabel;logo->setPixmap(QIcon(":/assets/logo.svg").pixmap(50,50));r->addWidget(logo);r->addSpacing(24);
        auto home=new QPushButton("✦");home->setToolTip("Chats im LAN");r->addWidget(home);r->addStretch();auto settingsButton=new QPushButton("⚙");settingsButton->setToolTip("Username ändern");r->addWidget(settingsButton);layout->addWidget(rail);
        auto side=new QFrame;side->setObjectName("sidebar");side->setFixedWidth(300);auto s=new QVBoxLayout(side);s->setContentsMargins(20,26,20,20);s->setSpacing(16);
        auto brand=new QLabel("ReChat <span style='color:#a898ef;font-size:12px'> / LOCAL</span>");brand->setStyleSheet("font-size:25px;font-weight:700");s->addWidget(brand);
        search=new QLineEdit;search->setPlaceholderText("⌕  Kontakte durchsuchen");s->addWidget(search);auto section=new QLabel("DIREKTNACHRICHTEN");section->setStyleSheet("color:#9692ac;font-size:11px;font-weight:600;letter-spacing:1px");s->addWidget(section);
        list=new QListWidget;s->addWidget(list,1);auto hint=new QLabel("Freunde finden sich automatisch,\nsobald ReChat im selben LAN läuft.");hint->setStyleSheet("color:#9692ac;font-size:12px");s->addWidget(hint);
        profile=new QLabel(net.name.isEmpty()?"Noch kein Username":"@"+net.name+"\nDein lokales Profil");s->addWidget(profile);auto claimButton=new QPushButton("Username claimen");s->addWidget(claimButton);layout->addWidget(side);
        auto main=new QWidget;auto m=new QVBoxLayout(main);m->setContentsMargins(30,26,30,20);m->setSpacing(16);
        title=new QLabel("Willkommen bei ReChat");title->setStyleSheet("font-size:21px;font-weight:650");m->addWidget(title);
        subtitle=new QLabel("Gemeinsam. Lokal. Ohne zentralen Server.");subtitle->setStyleSheet("color:#a09bb6");m->addWidget(subtitle);
        auto line=new QFrame;line->setFrameShape(QFrame::HLine);m->addWidget(line);
        chat=new QTextBrowser;chat->setOpenExternalLinks(false);chat->setFrameShape(QFrame::NoFrame);m->addWidget(chat,1);
        auto compose=new QHBoxLayout;input=new QLineEdit;input->setMaxLength(4000);input->setPlaceholderText("Schreib eine Nachricht …");compose->addWidget(input,1);sendButton=new QPushButton("Senden  ↗");sendButton->setObjectName("send");compose->addWidget(sendButton);m->addLayout(compose);
        status=new QLabel;status->setStyleSheet("color:#9994ad;font-size:11px");m->addWidget(status);layout->addWidget(main,1);
        connect(search,&QLineEdit::textChanged,this,[this]{refresh();});
        connect(list,&QListWidget::itemClicked,this,[this](QListWidgetItem *item){active=item->data(Qt::UserRole).toString();title->setText("@"+net.peers[active].name);refresh();render();});
        auto send=[this]{QString text=input->text().trimmed(),peer=active;if(text.isEmpty())return;sendButton->setEnabled(false);net.send(peer,text,[this,peer,text](bool ok){if(ok){add(peer,text,true);if(input->text().trimmed()==text)input->clear();}else QMessageBox::warning(this,"Nicht zugestellt","Das Gerät ist nicht erreichbar. Deine Nachricht bleibt im Eingabefeld.");refresh();});};
        connect(sendButton,&QPushButton::clicked,this,send);connect(input,&QLineEdit::returnPressed,this,[this,send]{if(sendButton->isEnabled())send();});
        connect(claimButton,&QPushButton::clicked,this,[this]{claim();});connect(settingsButton,&QPushButton::clicked,this,[this]{claim();});connect(home,&QPushButton::clicked,this,[this]{active.clear();title->setText("Willkommen bei ReChat");refresh();render();});
        net.changed=[this]{refresh();};net.message=[this](QString peer,QString text){add(peer,text,false);QApplication::alert(this);};
        net.conflict=[this](QString){settings.remove("username");profile->setText("Username-Konflikt · neu wählen");refresh();QMessageBox::information(this,"Username bereits belegt","Ein anderes Gerät reserviert diesen Namen. Bitte wähle einen neuen Username.");};
        bool started=net.start();refresh();if(!started)status->setText("LAN-Erkennung nicht verfügbar. Netzwerk / Firewall prüfen.");render();
        if(net.name.isEmpty())QTimer::singleShot(500,this,[this]{claim();});
    }
};
int main(int argc,char **argv) {
    QApplication app(argc,argv);app.setOrganizationName("ReChat");app.setApplicationName("ReChat");
    if(app.arguments().contains("--smoke-test")) {return validName("lyvt_dev")&&!validName("ab")&&!validName("invalid name")&&!validName("9abc")?0:1;}
    app.setFont(QFont("Segoe UI",10));
    app.setStyleSheet("QWidget{background:#191a23;color:#eeedf7;} QFrame#rail{background:#11121a;} QFrame#sidebar{background:#20212d;border-right:1px solid #30313d;} QLabel{background:transparent;} QPushButton{background:#30313f;border:0;border-radius:10px;padding:13px;font-weight:600;} QPushButton:hover{background:#45405f;} QPushButton#send{background:#8970ed;color:white;} QPushButton:disabled{background:#2b2b38;color:#747083;} QLineEdit{background:#272936;border:1px solid #363747;border-radius:10px;padding:14px;selection-background-color:#7862d6;} QLineEdit:focus{border:1px solid #a18af3;} QListWidget{background:transparent;border:0;outline:0;} QListWidget::item{border-radius:10px;padding:3px;} QListWidget::item:selected{background:#37304f;color:#e8ddff;} QListWidget::item:hover{background:#2e2d40;} QTextBrowser{background:transparent;border:0;} QScrollBar:vertical{background:transparent;width:7px;} QScrollBar::handle:vertical{background:#454052;border-radius:3px;min-height:30px;} QScrollBar::add-line:vertical,QScrollBar::sub-line:vertical{height:0;} QInputDialog,QMessageBox{background:#20212d;}");
    Window w;w.show();return app.exec();
}
