/**************************************************************************
**
** This file is part of Qt Creator
**
** Copyright (c) 2012 Nokia Corporation and/or its subsidiary(-ies).
**
** Contact: http://www.qt-project.org/
**
**
** GNU Lesser General Public License Usage
**
** This file may be used under the terms of the GNU Lesser General Public
** License version 2.1 as published by the Free Software Foundation and
** appearing in the file LICENSE.LGPL included in the packaging of this file.
** Please review the following information to ensure the GNU Lesser General
** Public License version 2.1 requirements will be met:
** http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html.
**
** In addition, as a special exception, Nokia gives you certain additional
** rights. These rights are described in the Nokia Qt LGPL Exception
** version 1.1, included in the file LGPL_EXCEPTION.txt in this package.
**
** Other Usage
**
** Alternatively, this file may be used in accordance with the terms and
** conditions contained in a signed written agreement between you and Nokia.
**
**
**************************************************************************/

#include "sshkeygenerator.h"

#include "ssh_global.h"
#include "sshbotanconversions_p.h"
#include "sshcapabilities_p.h"
#include "sshlogging_p.h"
#include "sshpacket_p.h"

#include <botan/auto_rng.h>
#include <botan/der_enc.h>
#include <botan/dl_group.h>
#include <botan/dsa.h>
#include <botan/ecdsa.h>
#include <botan/numthry.h>
#include <botan/pem.h>
#include <botan/pipe.h>
#include <botan/pkcs8.h>
#include <botan/rsa.h>
#include <botan/x509_key.h>
#include <botan/x509cert.h>

#include <QDateTime>
#include <QInputDialog>

#include <string>

namespace QSsh {

using namespace Botan;
using namespace Internal;

SshKeyGenerator::SshKeyGenerator()
    : m_type(Rsa)
{
}

bool SshKeyGenerator::generateKeys(KeyType type, PrivateKeyFormat format, int keySize,
    EncryptionMode encryptionMode)
{
    m_type = type;
    m_encryptionMode = encryptionMode;

    try {
        AutoSeeded_RNG rng;
        KeyPtr key;
        switch (m_type) {
        case Rsa:
            key = KeyPtr(new RSA_PrivateKey(rng, keySize));
            break;
        case Dsa:
            key = KeyPtr(new DSA_PrivateKey(rng, DL_Group(rng, DL_Group::DSA_Kosherizer, keySize)));
            break;
        case Ecdsa: {
                const QByteArray algo = SshCapabilities::ecdsaPubKeyAlgoForKeyWidth(keySize / 8);
                key = KeyPtr(new ECDSA_PrivateKey(rng, EC_Group(SshCapabilities::oid(algo))));
                break;
        }
        }
        switch (format) {
        case Pkcs8:
            generatePkcs8KeyStrings(key, rng);
            break;
        case OpenSsl:
            generateOpenSslKeyStrings(key);
            break;
        case Mixed:
        default:
            generatePkcs8KeyString(key, true, rng);
            generateOpenSslPublicKeyString(key);
        }
        return true;
    } catch (const std::exception& e) {
        m_error = tr("Error generating key: %1").arg(QString::fromLocal8Bit(e.what()));
        return false;
    }
}

void SshKeyGenerator::generatePkcs8KeyStrings(const KeyPtr& key, RandomNumberGenerator& rng)
{
    generatePkcs8KeyString(key, false, rng);
    generatePkcs8KeyString(key, true, rng);
}

void SshKeyGenerator::generatePkcs8KeyString(const KeyPtr& key, bool privateKey,
    RandomNumberGenerator& rng)
{
    Pipe pipe;
    pipe.start_msg();
    QByteArray* keyData;
    if (privateKey) {
        QString password;
        if (m_encryptionMode == DoOfferEncryption)
            password = getPassword();
        if (!password.isEmpty())
            pipe.write(PKCS8::PEM_encode(*key, rng, password.toLocal8Bit().data()));
        else
            pipe.write(PKCS8::PEM_encode(*key));
        keyData = &m_privateKey;
    } else {
        pipe.write(X509::PEM_encode(*key));
        keyData = &m_publicKey;
    }
    pipe.end_msg();
    keyData->resize(static_cast<int>(pipe.remaining(pipe.message_count() - 1)));
    size_t readSize = pipe.read(convertByteArray(*keyData), keyData->size(),
        pipe.message_count() - 1);
    if (readSize != size_t(keyData->size())) {
        qCWarning(sshLog, "Didn't manage to read in all key data, only read %lu bytes", readSize);
    }
}

void SshKeyGenerator::generateOpenSslKeyStrings(const KeyPtr& key)
{
    generateOpenSslPublicKeyString(key);
    generateOpenSslPrivateKeyString(key);
}

void SshKeyGenerator::generateOpenSslPublicKeyString(const KeyPtr& key)
{
    QList<BigInt> params;
    QByteArray keyId;
    QByteArray q;
    switch (m_type) {
    case Rsa: {
        const QSharedPointer<RSA_PrivateKey> rsaKey = key.dynamicCast<RSA_PrivateKey>();
        params << rsaKey->get_e() << rsaKey->get_n();
        keyId = SshCapabilities::PubKeyRsa;
        break;
    }
    case Dsa: {
        const QSharedPointer<DSA_PrivateKey> dsaKey = key.dynamicCast<DSA_PrivateKey>();
        params << dsaKey->get_int_field("p") << dsaKey->get_int_field("q") 
               << dsaKey->get_int_field("g") << dsaKey->get_int_field("y");
        keyId = SshCapabilities::PubKeyDss;
        break;
    }
    case Ecdsa: {
        const auto ecdsaKey = key.dynamicCast<ECDSA_PrivateKey>();
        q = convertByteArray(ecdsaKey->public_point().encode(EC_Point_Format::Uncompressed));
        keyId = SshCapabilities::ecdsaPubKeyAlgoForKeyWidth(
            static_cast<int>(ecdsaKey->private_value().bytes()));
        break;
    }
    }

    QByteArray publicKeyBlob = AbstractSshPacket::encodeString(keyId);
    for (const BigInt& b : params) {
        publicKeyBlob += AbstractSshPacket::encodeMpInt(b);
    }
    if (!q.isEmpty()) {
        publicKeyBlob += AbstractSshPacket::encodeString(keyId.mid(11)); // Without "ecdsa-sha2-" prefix.
        publicKeyBlob += AbstractSshPacket::encodeString(q);
    }
    publicKeyBlob = publicKeyBlob.toBase64();
    const QByteArray id = "QtCreator/"
        + QDateTime::currentDateTime().toString(Qt::ISODate).toUtf8();
    m_publicKey = keyId + ' ' + publicKeyBlob + ' ' + id;
}

void SshKeyGenerator::generateOpenSslPrivateKeyString(const KeyPtr& key)
{
    QList<BigInt> params;
    const char* label = "";
    switch (m_type) {
    case Rsa: {
        const QSharedPointer<RSA_PrivateKey> rsaKey
            = key.dynamicCast<RSA_PrivateKey>();
        params << rsaKey->get_n() << rsaKey->get_e() << rsaKey->get_d() << rsaKey->get_p()
               << rsaKey->get_q();
        const BigInt dmp1 = rsaKey->get_d() % (rsaKey->get_p() - 1);
        const BigInt dmq1 = rsaKey->get_d() % (rsaKey->get_q() - 1);
        const BigInt iqmp = inverse_mod(rsaKey->get_q(), rsaKey->get_p());
        params << dmp1 << dmq1 << iqmp;
        label = "RSA PRIVATE KEY";
        break;
    }
    case Dsa: {
        const QSharedPointer<DSA_PrivateKey> dsaKey = key.dynamicCast<DSA_PrivateKey>();
        params << dsaKey->get_int_field("p") << dsaKey->get_int_field("q") 
               << dsaKey->get_int_field("g") << dsaKey->get_int_field("y")
               << dsaKey->get_int_field("x");
        label = "DSA PRIVATE KEY";
        break;
    }
    case Ecdsa:
        params << key.dynamicCast<ECDSA_PrivateKey>()->private_value();
        label = "EC PRIVATE KEY";
        break;
    }

    DER_Encoder encoder;
    encoder.start_cons(Botan::ASN1_Type::Sequence, Botan::ASN1_Class::Universal).encode(size_t(0));
    for (const BigInt& b : params) {
        encoder.encode(b);
    }
    encoder.end_cons();
    m_privateKey = QByteArray(PEM_Code::encode(encoder.get_contents(), label).c_str());
}

QString SshKeyGenerator::getPassword() const
{
    QInputDialog d;
    d.setInputMode(QInputDialog::TextInput);
    d.setTextEchoMode(QLineEdit::Password);
    d.setWindowTitle(tr("Password for Private Key"));
    d.setLabelText(tr("It is recommended that you secure your private key\n"
                      "with a password, which you can enter below."));
    d.setOkButtonText(tr("Encrypt Key File"));
    d.setCancelButtonText(tr("Do Not Encrypt Key File"));
    int result = QDialog::Accepted;
    QString password;
    while (result == QDialog::Accepted && password.isEmpty()) {
        result = d.exec();
        password = d.textValue();
    }
    return result == QDialog::Accepted ? password : QString();
}

} // namespace QSsh
