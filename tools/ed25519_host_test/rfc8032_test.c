#include <stdio.h>
#include <string.h>
#include "ed25519_orlp.h"
static void hx(const char*h,unsigned char*o,int n){for(int i=0;i<n;i++)sscanf(h+2*i,"%2hhx",o+i);}
int main(){
 struct {const char*sk,*pk,*msg,*sig;} v[]={
 {"9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60","d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a","",
  "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"},
 {"4ccd089b28ff96da9db6c346ec114e0f5b8a319f35aba624da8cf6ed4fb8a6fb","3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c","72",
  "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"},
 {"c5aa8df43f9f837bedb7442f31dcb7b166d38535076f094b85ce3a2e0b4458f7","fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025","af82",
  "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a"}};
 int bad=0;
 for(int t=0;t<3;t++){unsigned char seed[32],pk[32],sk[64],epk[32],sig[64],esig[64],msg[8];int ml=strlen(v[t].msg)/2;
  hx(v[t].sk,seed,32);hx(v[t].pk,epk,32);hx(v[t].sig,esig,64);hx(v[t].msg,msg,ml);
  ed25519_create_keypair(pk,sk,seed); ed25519_sign(sig,msg,ml,pk,sk);
  int ok=!memcmp(pk,epk,32)&&!memcmp(sig,esig,64)&&ed25519_verify(sig,msg,ml,pk); sig[5]^=1; int rej=!ed25519_verify(sig,msg,ml,pk);
  printf("RFC8032 test %d: sign=%s tampered-rejected=%s\n",t+1,ok?"OK":"FAIL",rej?"OK":"FAIL"); bad+=!ok||!rej;}
 return bad;}
