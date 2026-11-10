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
template<class Transport> const char *smtp_send(Transport &io,const char *user,const char *password,const char *to,const std::string &body,const char *illustration=nullptr){
    auto command=[&](const std::string &s,int code){return io.write_all(s+"\r\n")&&smtp_reply(io)==code;};
    if(smtp_reply(io)!=220)return "Gmail greeting failed";
    if(!command("EHLO safe-road.local",250))return "Gmail EHLO failed";
    if(!command("AUTH LOGIN",334)||!command(base64(user),334)||!command(base64(password),235))return "Gmail authentication failed; check app password";
    if(!command(std::string("MAIL FROM:<")+user+">",250))return "Gmail rejected sender";
    if(!command(std::string("RCPT TO:<")+to+">",250))return "Gmail rejected recipient";
    if(!command("DATA",354))return "Gmail DATA failed";
    constexpr char boundary[]="=_safe_road_event";
    std::string message=std::string("From: <")+user+">\r\nTo: <"+to+">\r\nSubject: SAFE ROAD monitoring event\r\nMIME-Version: 1.0\r\nContent-Type: multipart/alternative; boundary=\""+boundary+"\"\r\n\r\n--"+boundary+"\r\nContent-Type: text/plain; charset=UTF-8\r\nContent-Transfer-Encoding: 8bit\r\n\r\n";
    bool start=true;
    for(char c:body){if(start&&c=='.')message+='.';if(c=='\n'){message+="\r\n";start=true;}else if(c!='\r'){message+=c;start=false;}}
    if(message.size()<2||message.substr(message.size()-2)!="\r\n")message+="\r\n";
    message+=std::string("--")+boundary+"\r\n";
    if(illustration){
        message+="Content-Type: multipart/related; type=\"text/html\"; boundary=\"=_safe_road_image\"\r\n\r\n"
            "--=_safe_road_image\r\n";
    }
    message+="Content-Type: text/html; charset=UTF-8\r\nContent-Transfer-Encoding: 8bit\r\n\r\n"
        "<!doctype html><html lang=\"en\"><body style=\"margin:0;padding:24px;background:#fff1f2;font-family:Arial,sans-serif;color:#3b2024\">\r\n"
        "<div style=\"max-width:560px;margin:auto;background:#fff;border:1px solid #efc5ca;border-radius:16px;overflow:hidden\">\r\n"
        "<div style=\"background:#a11228;color:#fff;padding:28px 24px;border-bottom:5px solid #ed5367\">\r\n"
        "<p style=\"display:inline-block;margin:0 0 12px;padding:5px 9px;background:#750d20;color:#fff;font-size:12px;font-weight:bold;letter-spacing:2px;border-radius:4px\">SAFE ROAD</p>\r\n"
        "<h1 style=\"margin:0;font-size:28px;line-height:1.2\">Monitoring notification</h1></div>\r\n"
        "<div style=\"padding:24px;font-size:15px;line-height:1.6\">\r\n";
    if(illustration){
        message+="<div style=\"margin:0 0 24px;padding:18px;text-align:center;background:#fff1f2;border:1px solid #efc5ca;border-radius:12px\">\r\n"
            "<img src=\"cid:detected-event@safe-road.local\" width=\"168\" height=\"116\" alt=\"Detection illustration\"></div>\r\n";
    }
    message+="<h2 style=\"margin:0 0 12px;padding-bottom:8px;border-bottom:1px solid #efc5ca;color:#a11228;font-size:15px\">Event details</h2>\r\n";
    // The plain-text body starts with the brand, which is already in the HTML header.
    const size_t details=body.find("SAFE ROAD\n")==0?10:0;
    for(size_t i=details;i<body.size();++i){
        if(body[i]=='.'&&(i==details||body[i-1]=='\n'))message+='.';
        switch(body[i]){
        case '&': message+="&amp;"; break;
        case '<': message+="&lt;"; break;
        case '>': message+="&gt;"; break;
        case '\n': message+="<br>\r\n"; break;
        case '\r': break;
        default: message+=body[i]; break;
        }
    }
    message+="</div></div></body></html>\r\n";
    if(illustration){
        message+="--=_safe_road_image\r\nContent-Type: image/png\r\nContent-Transfer-Encoding: base64\r\n"
            "Content-ID: <detected-event@safe-road.local>\r\nContent-Disposition: inline\r\n\r\n";
        message+=illustration;
        message+="--=_safe_road_image--\r\n";
    }
    message+=std::string("--")+boundary+"--\r\n";
    if(!io.write_all(message+".\r\n")||smtp_reply(io)!=250)return "Gmail did not accept message";
    // Acceptance was confirmed; a failed QUIT must not cause duplicate delivery.
    io.write_all("QUIT\r\n");return nullptr;
}
}
