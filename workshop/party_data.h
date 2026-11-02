#pragma once
#include "project.h"
#include "mod/party.h"
#include "mod/types.h"
#include "mod/package.h"
namespace Workshop {
inline void finishParty(QByteArray &b) {
    TYPES_Put32(b.data()+16,MOD_Crc32(b.constData()+32,b.size()-32));
}
inline QByteArray partyData(const Project &p) {
    auto b=p.resources.value("PARTY.OVR").edited;
    if(b.isEmpty()) {
        b=QByteArray(32+16*PARTY_RECORD_SIZE,0);b.replace(0,8,"IPARTY01");
        TYPES_Put32(b.data()+8,16);TYPES_Put32(b.data()+12,6);
    }
    for(int i=0;i<16;i++) b.replace(32+i*PARTY_RECORD_SIZE,32,p.data("INIT.GAM").mid(2+i*32,32));
    finishParty(b);return b;
}
inline QMap<QString,QByteArray> partyEdits(const Project &p,QByteArray b) {
    finishParty(b);auto init=p.data("INIT.GAM");
    for(int i=0;i<16;i++) init.replace(2+i*32,32,b.mid(32+i*PARTY_RECORD_SIZE,32));
    return {{"INIT.GAM",init},{"PARTY.OVR",b}};
}
}
