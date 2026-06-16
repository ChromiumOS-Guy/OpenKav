#ifndef ISO7816APDU_H
#define ISO7816APDU_H

#include <QByteArray>
#include <cstdint>

class Iso7816Apdu {
public:
    struct Response {
        QByteArray data;
        uint8_t sw1 = 0x00;
        uint8_t sw2 = 0x00;

        bool isSuccess() const { return sw1 == 0x90 && sw2 == 0x00; }
        bool hasMoreData() const { return sw1 == 0x61; }
        bool hasWrongLength() const { return sw1 == 0x6C; }
        uint8_t bytesAvailableOrExpected() const { return sw2; }
    };

    static Response parseResponse(const QByteArray &rawFrame) {
        Response resp;
        if (rawFrame.size() < 2) return resp;

        resp.sw1 = static_cast<uint8_t>(rawFrame.at(rawFrame.size() - 2));
        resp.sw2 = static_cast<uint8_t>(rawFrame.at(rawFrame.size() - 1));
        resp.data = rawFrame.left(rawFrame.size() - 2);
        return resp;
    }

    static QByteArray createGetResponse(uint8_t length) {
        QByteArray cmd;
        cmd.append(static_cast<char>(0x00)); // CLA
        cmd.append(static_cast<char>(0xC0)); // INS (GET RESPONSE)
        cmd.append(static_cast<char>(0x00)); // P1
        cmd.append(static_cast<char>(0x00)); // P2
        cmd.append(static_cast<char>(length)); // Le
        return cmd;
    }

    static QByteArray mutateLe(const QByteArray &originalCmd, uint8_t correctLength) {
        QByteArray mutated = originalCmd;
        if (mutated.size() == 4) {
            mutated.append(static_cast<char>(correctLength));
        } else if (mutated.size() >= 5) {
            mutated[mutated.size() - 1] = static_cast<char>(correctLength);
        }
        return mutated;
    }
};

#endif // ISO7816APDU_H
