#include "../main/alert_policy.hpp"
#include "../main/smtp_protocol.hpp"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <iostream>
struct Transport {
 std::string input,output;size_t offset=0;
 bool read_char(char &c){if(offset==input.size())return false;c=input[offset++];return true;}
 bool write_all(const std::string &s){output+=s;return true;}
};
int main(){
 using namespace alerts;
 Gate custom;assert(!custom.take(0,1999,10000,60,2000));assert(custom.take(0,2000,10001,60,2000));
 assert(valid_hold_ms(500)&&valid_hold_ms(60000));assert(!valid_hold_ms(499)&&!valid_hold_ms(60001));
 Gate g;
 assert(!g.take(0,1499,10000,60));assert(g.take(0,1500,10500,60));assert(!g.take(0,3000,12000,60));
 assert(!g.take(0,0,13000,60));assert(!g.take(0,1500,14000,60));assert(!g.take(0,90000,100000,60));
 assert(!g.take(0,0,101000,60));assert(g.take(0,1500,102500,60));
 assert(g.take(1,3000,103000,60));g.reset();assert(!g.take(1,3000,104000,60));
 assert(email_address("driver@gmail.com"));assert(email_address("a.b+tag@example.org"));
 for(auto bad:{"a\r\nBcc:x@evil.com","a@b","a@@b.com","a b@c.com","@example.com"})assert(!email_address(bad));
 assert(https_url("https://example.com/hooks/abc?token=xyz"));
 for(auto bad:{"http://example.com","https://","https:///x","https://user:pass@example.com","https://x.com/\r\nx","https://x.com/#secret"})assert(!https_url(bad));
 assert(base64("f")=="Zg==");assert(base64("fo")=="Zm8=");assert(base64("foo")=="Zm9v");
 const std::string ok="220 ready\r\n250-mail.example\r\n250 AUTH LOGIN\r\n334 user\r\n334 pass\r\n235 authenticated\r\n250 sender\r\n250 recipient\r\n354 data\r\n250 accepted\r\n";
 Transport t{ok,""};assert(!smtp_send(t,"sender@gmail.com","abcdefghijklmnop","to@example.com","Test\n.leading dot\n"));
 assert(t.output.find("RCPT TO:<to@example.com>\r\n")!=std::string::npos);
 assert(t.output.find("Test\r\n..leading dot\r\n.\r\nQUIT\r\n")!=std::string::npos);
 assert(t.output.find("abcdefghijklmnop")==std::string::npos);
 Transport rejected{"220 ready\r\n250 ok\r\n334 user\r\n334 pass\r\n535 denied\r\n",""};
 assert(std::string(smtp_send(rejected,"u","p","t","body")).find("authentication")!=std::string::npos);
 assert(rejected.output.find("MAIL FROM")==std::string::npos);
 Transport malformed{"250-one\r\n550 wrong code\r\n",""};assert(smtp_reply(malformed)==-1);
 Transport longline{std::string(600,'a'),""};assert(smtp_reply(longline)==-1);
 Transport lost{ok.substr(0,ok.rfind("250 accepted")),""};assert(smtp_send(lost,"u","p","t","body"));
 std::cout<<"PASS: event thresholds, once per episode, cooldown/rearm, input validation, SMTP multiline/auth/body/acceptance errors\n";
}
