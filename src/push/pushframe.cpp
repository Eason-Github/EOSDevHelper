#include "pushframe.h"
#include "ui_pushframe.h"
#include "utility/httpclient.h"
#include "mainwindow.h"
#include "chain/chainmanager.h"
#include "chain/packedtransaction.h"
#include "wallet/eoswalletmanager.h"

#include <QFileDialog>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMessageBox>

extern MainWindow *w;

PushFrame::PushFrame(QWidget *parent) :
    QFrame(parent),
    ui(new Ui::PushFrame),
    httpc(new HttpClient)
{
    ui->setupUi(this);
    ui->lineEditContractAccount->setValidator(new QRegExpValidator(QRegExp(eos_account_regex), this));
}

PushFrame::~PushFrame()
{
    delete ui;
    delete httpc;
}

QByteArray PushFrame::packAbiJsonToBinParam()
{
    QByteArray param;

    QString args = ui->textEditAction->toPlainText();
    if (args.isEmpty()) {
        // action args should NOT be empty for now.
        QMessageBox::warning(nullptr, "Error", "Empty action args!");
        return param;
    }

    QByteArray ba = QByteArray::fromStdString(args.toStdString());
    QJsonDocument doc = QJsonDocument::fromJson(ba);
    if (doc.isNull()) {
        QMessageBox::warning(nullptr, "Error", "Wrong json format!");
        return param;
    }

    QString code = ui->lineEditContractAccount->text();
    QString action = ui->lineEditContractAction->text();

    QJsonObject obj;
    obj.insert("code", QJsonValue(code));
    obj.insert("action", QJsonValue(action));
    obj.insert("args", doc.object());

    param = QJsonDocument(obj).toJson();
    return param;
}

QByteArray PushFrame::packGetRequiredKeysParam()
{
    QJsonObject abiBinObj = QJsonDocument::fromJson(abiJsonToBinData).object();
    QString binargs = abiBinObj.value("binargs").toString();
    QString code = ui->lineEditContractAccount->text();
    QString action = ui->lineEditContractAction->text();

    signedTxn = ChainManager::createTransaction(code.toStdString(), action.toStdString(), binargs.toStdString(), ChainManager::getActivePermission(EOS_SYSTEM_ACCOUNT), getInfoData);

    QJsonObject txnObj = signedTxn.toJson().toObject();

    QJsonArray avaibleKeys;
    QMap<QString, EOSWallet> UnlockedWallets = EOSWalletManager::instance().listKeys(EOSWalletManager::ws_unlocked);
    for (const auto& w : UnlockedWallets) {
        QMap<QString, QString> keys = w.listKeys();
        QStringList list = keys.keys();
        for (int i = 0; i < list.size(); ++i) {
            avaibleKeys.append(QJsonValue(list.at(i)));
        }
    }

    QJsonObject obj;
    obj.insert("available_keys", avaibleKeys);
    obj.insert("transaction", txnObj);
    return QJsonDocument(obj).toJson();
}

QByteArray PushFrame::packPushTransactionParam()
{
    QJsonArray array = QJsonDocument::fromJson(getRequiredKeysData).object().value("required_keys").toArray();
    if (!array.size()) {
        return QByteArray();
    }

    std::vector<std::string> keys;
    for (int i = 0; i < array.size(); ++i) {
        std::string key = array.at(i).toString().toStdString();
        keys.push_back(key);
    }

    EOSWalletManager::instance().signTransaction(signedTxn, keys, TypeChainId());
    PackedTransaction packedTxn(signedTxn, "none");

    QJsonObject obj = packedTxn.toJson().toObject();

    return QJsonDocument(obj).toJson();
}

void PushFrame::on_pushButtonImportFile_clicked()
{
    QString filePath = QFileDialog::getOpenFileName(this, "Choose file", "");
    if (filePath.isEmpty())
        return;

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return;

    QTextStream in(&file);
    QString data = in.readAll();    // yeah, since we want import file and edit the content, we just read all.

    QByteArray ba = QByteArray::fromStdString(data.toStdString());
    QJsonDocument doc = QJsonDocument::fromJson(ba);
    QByteArray formatedData = doc.toJson(QJsonDocument::Indented);
    ui->textEditAction->setText(QString::fromStdString(formatedData.toStdString()));
}

void PushFrame::on_pushButtonSend_clicked()
{
    QByteArray param = packAbiJsonToBinParam();
    if (param.isEmpty()) {
        return;
    }

    w->pushOutputFrame()->setRequestOutput(0, "abi_json_to_bin", param);

    if (httpc) {
        httpc->abi_json_to_bin(QString::fromStdString(param.toStdString()));
        connect(httpc, &HttpClient::responseData, this, &PushFrame::abi_json_to_bin_returned);
    }
}

void PushFrame::abi_json_to_bin_returned(const QByteArray &data)
{
    disconnect(httpc, &HttpClient::responseData, this, &PushFrame::abi_json_to_bin_returned);

    w->pushOutputFrame()->setResponseOutput(0, data);

    abiJsonToBinData.clear();
    abiJsonToBinData = data;

    w->pushOutputFrame()->setRequestOutput(1, "get_info", QByteArray());

    if (httpc) {
        httpc->get_info();
        connect(httpc, &HttpClient::responseData, this, &PushFrame::get_info_returned);
    }
}

void PushFrame::get_info_returned(const QByteArray &data)
{
    disconnect(httpc, &HttpClient::responseData, this, &PushFrame::get_info_returned);

    w->pushOutputFrame()->setResponseOutput(1, data);

    getInfoData.clear();
    getInfoData = data;

    QByteArray param = packGetRequiredKeysParam();
    if (param.isEmpty()) {
        return;
    }

    w->pushOutputFrame()->setRequestOutput(2, "get_required_keys", param);

    if (httpc) {
        httpc->get_required_keys(QString::fromStdString(param.toStdString()));
        connect(httpc, &HttpClient::responseData, this, &PushFrame::get_required_keys_returned);
    }
}

void PushFrame::get_required_keys_returned(const QByteArray &data)
{
    disconnect(httpc, &HttpClient::responseData, this, &PushFrame::get_required_keys_returned);

    w->pushOutputFrame()->setResponseOutput(2, data);

    getRequiredKeysData.clear();
    getRequiredKeysData = data;

    QByteArray param = packPushTransactionParam();
    if (param.isEmpty()) {
        return;
    }

    w->pushOutputFrame()->setRequestOutput(3, "push_transaction", param);

    if (httpc) {
        httpc->push_transaction(QString::fromStdString(param.toStdString()));
        connect(httpc, &HttpClient::responseData, [=](const QByteArray& data){
            w->pushOutputFrame()->setResponseOutput(3, data);
        });
    }
}
