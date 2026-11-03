#pragma once
#include <string>
#include <cstdio>
#include <cctype>
namespace alerts {
inline std::string base64(const std::string &s){
    constexpr char chars[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for(size_t i=0;i<s.size();i+=3){
        unsigned v=unsigned((unsigned char)s[i])<<16;
        if(i+1<s.size())v|=unsigned((unsigned char)s[i+1])<<8;
        if(i+2<s.size())v|=(unsigned char)s[i+2];
        out+=chars[(v>>18)&63];out+=chars[(v>>12)&63];
        out+=i+1<s.size()?chars[(v>>6)&63]:'=';out+=i+2<s.size()?chars[v&63]:'=';
    }
    return out;
}
// Transport read/write must enforce timeouts. Never surface server text/credentials.
template<class Transport> int smtp_reply(Transport &io){
    int expected=0;
    for(int line=0;line<32;line++){
        std::string s;
        for(int i=0;i<512;i++){char c;if(!io.read_char(c))return -1;s+=c;if(c=='\n')break;}
        if(s.size()<5||s.back()!='\n'||!std::isdigit((unsigned char)s[0])||!std::isdigit((unsigned char)s[1])||!std::isdigit((unsigned char)s[2]))return -1;
        int code=(s[0]-'0')*100+(s[1]-'0')*10+s[2]-'0';
        if(expected&&code!=expected)return -1;
        expected=code;
        if(s[3]==' ')return code;
        if(s[3]!='-')return -1;
    }
    return -1;
}
template<class Transport> const char *smtp_send(Transport &io,const char *user,const char *password,const char *to,const std::string &body){
    auto command=[&](const std::string &s,int code){return io.write_all(s+"\r\n")&&smtp_reply(io)==code;};
    if(smtp_reply(io)!=220)return "Gmail greeting failed";
    if(!command("EHLO safe-road.local",250))return "Gmail EHLO failed";
    if(!command("AUTH LOGIN",334)||!command(base64(user),334)||!command(base64(password),235))return "Gmail authentication failed; check app password";
    if(!command(std::string("MAIL FROM:<")+user+">",250))return "Gmail rejected sender";
    if(!command(std::string("RCPT TO:<")+to+">",250))return "Gmail rejected recipient";
    if(!command("DATA",354))return "Gmail DATA failed";
    std::string message=std::string("From: <")+user+">\r\nTo: <"+to+">\r\nSubject: SAFE ROAD monitoring event\r\nMIME-Version: 1.0\r\nContent-Type: text/plain; charset=UTF-8\r\nContent-Transfer-Encoding: 8bit\r\n\r\n";
    bool start=true;
    for(char c:body){if(start&&c=='.')message+='.';if(c=='\n'){message+="\r\n";start=true;}else if(c!='\r'){message+=c;start=false;}}
    if(message.size()<2||message.substr(message.size()-2)!="\r\n")message+="\r\n";
    if(!io.write_all(message+".\r\n")||smtp_reply(io)!=250)return "Gmail did not accept message";
    // Acceptance was confirmed; a failed QUIT must not cause duplicate delivery.
    io.write_all("QUIT\r\n");return nullptr;
}
}
