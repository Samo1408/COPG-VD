#include <jni.h>
#include <android/log.h>
#include <lsplant.hpp>
#include <string>
#include <vector>
#include <fstream>
#include <algorithm>
#include <cctype>
#include "json.hpp"

using json = nlohmann::json;
#define TAG "COPG-VD/DeviceHooks"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)

namespace copg_lsplant { bool init(JNIEnv* env); }

namespace copg_device_hooks {
static const char* CFG = "/data/adb/COPG-VD.json";
static bool g_installed = false;
static std::vector<jobject> g_hookers;

struct DeviceCfg {
    std::string wifi;
    std::string drm;
    std::string simOperator;
    std::string simOperatorName;
    std::string simCountryIso;
    std::string networkOperator;
    std::string networkOperatorName;
    std::string simSerial;
    std::string subscriberId;
    std::string line1Number;
};

static DeviceCfg load() {
    DeviceCfg c;
    std::ifstream f(CFG);
    if (!f) return c;
    try {
        json j = json::parse(f);
        auto d = j.contains("COPG-VD") && j["COPG-VD"].is_object() ? j["COPG-VD"] : json::object();
        c.wifi = d.value("WIFI_SSID", "");
        c.drm = d.value("DRM_ID", "");
        c.simOperator = d.value("SIM_OPERATOR", "");
        c.simOperatorName = d.value("SIM_OPERATOR_NAME", "");
        c.simCountryIso = d.value("SIM_COUNTRY_ISO", "");
        c.networkOperator = d.value("NETWORK_OPERATOR", "");
        c.networkOperatorName = d.value("NETWORK_OPERATOR_NAME", "");
        c.simSerial = d.value("SIM_SERIAL", "");
        c.subscriberId = d.value("SUBSCRIBER_ID", "");
        c.line1Number = d.value("LINE1_NUMBER", "");
    } catch (...) { LOGE("Config parse failed"); }
    return c;
}

static jclass find(JNIEnv* e, const char* n) { jclass c=e->FindClass(n); if(!c) e->ExceptionClear(); return c; }

static jobject makeConstantHooker(JNIEnv* e, jobject value, int argc) {
    if (!e || !value) return nullptr;
    jclass mh = find(e, "java/lang/invoke/MethodHandles");
    jclass obj = find(e, "java/lang/Object");
    if (!mh || !obj) return nullptr;
    jmethodID constant = e->GetStaticMethodID(mh, "constant", "(Ljava/lang/Class;Ljava/lang/Object;)Ljava/lang/invoke/MethodHandle;");
    jmethodID drop = e->GetStaticMethodID(mh, "dropArguments", "(Ljava/lang/invoke/MethodHandle;I[Ljava/lang/Class;)Ljava/lang/invoke/MethodHandle;");
    if (!constant || !drop) { e->ExceptionClear(); return nullptr; }
    jobject h = e->CallStaticObjectMethod(mh, constant, obj, value);
    if (e->ExceptionCheck() || !h) { e->ExceptionClear(); return nullptr; }
    jobjectArray types = e->NewObjectArray(argc, obj, obj);
    jobject out = e->CallStaticObjectMethod(mh, drop, h, 0, types);
    e->DeleteLocalRef(types); e->DeleteLocalRef(h); e->DeleteLocalRef(mh); e->DeleteLocalRef(obj);
    if (e->ExceptionCheck()) { e->ExceptionClear(); return nullptr; }
    return out;
}

static jobject newString(JNIEnv* e, const std::string& s) { return e->NewStringUTF(s.c_str()); }

static jobject hexOrUtf8Bytes(JNIEnv* e, const std::string& s) {
    bool hex = !s.empty() && s.size()%2==0 && std::all_of(s.begin(),s.end(),[](unsigned char c){return std::isxdigit(c);});
    if (!hex) return newString(e,s); // converted below only for string-returning hooks
    jbyteArray a=e->NewByteArray((jsize)s.size()/2); std::vector<jbyte>b(s.size()/2);
    auto val=[](char c)->int{if(c>='0'&&c<='9')return c-'0'; if(c>='a'&&c<='f')return c-'a'+10; return c-'A'+10;};
    for(size_t i=0;i<b.size();++i)b[i]=(jbyte)((val(s[i*2])<<4)|val(s[i*2+1]));
    e->SetByteArrayRegion(a,0,(jsize)b.size(),b.data()); return a;
}

static bool hook(JNIEnv* e, const char* cls, const char* name, const char* sig, jobject value, int argc) {
    jclass c=find(e,cls); if(!c)return false;
    jclass cc=find(e,"java/lang/Class"); if(!cc)return false;
    jmethodID get=e->GetMethodID(cc,"getDeclaredMethod","(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;");
    jclass mh=find(e,"java/lang/invoke/MethodHandle");
    jmethodID cb=mh?e->GetMethodID(mh,"invokeWithArguments","([Ljava/lang/Object;)Ljava/lang/Object;"):nullptr;
    if(!get||!cb){e->ExceptionClear();return false;}
    jobjectArray p=e->NewObjectArray(argc,cc,nullptr);
    // All currently targeted methods are no-arg except TelephonyManager#getNetworkOperatorName etc.
    for(int i=0;i<argc;i++) e->SetObjectArrayElement(p,i,find(e,"java/lang/String"));
    jstring n=e->NewStringUTF(name);
    jobject m=e->CallObjectMethod(c,get,n,p); e->DeleteLocalRef(p); e->DeleteLocalRef(n);
    if(e->ExceptionCheck()||!m){e->ExceptionClear();return false;}
    jobject h=makeConstantHooker(e,value,argc); if(!h)return false;
    jobject backup=lsplant::Hook(e,m,h,cb);
    if(!backup){LOGE("hook failed %s.%s",cls,name);return false;}
    g_hookers.push_back(e->NewGlobalRef(h));
    LOGI("hooked %s.%s",cls,name); return true;
}

static void installWifi(JNIEnv* e,const DeviceCfg& c) {
    if(c.wifi.empty())return;
    jclass wifi=find(e,"android/net/wifi/WifiInfo"); if(!wifi)return;
    jclass cc=find(e,"java/lang/Class"); jmethodID get=e->GetMethodID(cc,"getDeclaredMethod","(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;");
    jclass mh=find(e,"java/lang/invoke/MethodHandle"); jmethodID cb=mh?e->GetMethodID(mh,"invokeWithArguments","([Ljava/lang/Object;)Ljava/lang/Object;"):nullptr;
    jobjectArray p=e->NewObjectArray(0,cc,nullptr); jstring n=e->NewStringUTF("getSSID"); jobject m=get?e->CallObjectMethod(wifi,get,n,p):nullptr;
    if(e->ExceptionCheck()){e->ExceptionClear();m=nullptr;} e->DeleteLocalRef(p);e->DeleteLocalRef(n);
    jobject v=newString(e,c.wifi); if(m&&v){jobject h=makeConstantHooker(e,v,1); if(h){jobject b=lsplant::Hook(e,m,h,cb); if(b){g_hookers.push_back(e->NewGlobalRef(h));LOGI("hooked WifiInfo.getSSID");}}}
}

static jobject buildGuardedConstant(JNIEnv* e, jobject originalMethod, jobject value, int argc, int testArgIndex) {
    jclass methodHandles=find(e,"java/lang/invoke/MethodHandles");
    jclass lookupClass=find(e,"java/lang/invoke/MethodHandles$Lookup");
    jclass methodClass=find(e,"java/lang/reflect/Method");
    jclass mhClass=find(e,"java/lang/invoke/MethodHandle");
    jclass mtClass=find(e,"java/lang/invoke/MethodType");
    jclass objClass=find(e,"java/lang/Object");
    jclass strClass=find(e,"java/lang/String");
    jclass boolClass=find(e,"java/lang/Boolean");
    if(!methodHandles||!lookupClass||!methodClass||!mhClass||!mtClass||!objClass||!strClass||!boolClass)return nullptr;
    jmethodID lookupId=e->GetStaticMethodID(methodHandles,"lookup","()Ljava/lang/invoke/MethodHandles$Lookup;");
    jmethodID unreflect=e->GetMethodID(lookupClass,"unreflect","(Ljava/lang/reflect/Method;)Ljava/lang/invoke/MethodHandle;");
    jmethodID asType=e->GetMethodID(mhClass,"asType","(Ljava/lang/invoke/MethodType;)Ljava/lang/invoke/MethodHandle;");
    jmethodID constant=e->GetStaticMethodID(methodHandles,"constant","(Ljava/lang/Class;Ljava/lang/Object;)Ljava/lang/invoke/MethodHandle;");
    jmethodID drop=e->GetStaticMethodID(methodHandles,"dropArguments","(Ljava/lang/invoke/MethodHandle;I[Ljava/lang/Class;)Ljava/lang/invoke/MethodHandle;");
    jmethodID getDeclared=e->GetMethodID(methodClass,"getDeclaredMethod","(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;");
    jmethodID insert=e->GetStaticMethodID(methodHandles,"insertArguments","(Ljava/lang/invoke/MethodHandle;I[Ljava/lang/Object;)Ljava/lang/invoke/MethodHandle;");
    jmethodID permute=e->GetStaticMethodID(methodHandles,"permuteArguments","(Ljava/lang/invoke/MethodHandle;Ljava/lang/invoke/MethodType;[I)Ljava/lang/invoke/MethodHandle;");
    jmethodID guard=e->GetStaticMethodID(methodHandles,"guardWithTest","(Ljava/lang/invoke/MethodHandle;Ljava/lang/invoke/MethodHandle;Ljava/lang/invoke/MethodHandle;)Ljava/lang/invoke/MethodHandle;");
    if(!lookupId||!unreflect||!asType||!constant||!drop||!getDeclared||!insert||!permute||!guard){e->ExceptionClear();return nullptr;}
    jobject lookup=e->CallStaticObjectMethod(methodHandles,lookupId);
    jobject original=e->CallObjectMethod(lookup,unreflect,originalMethod);
    jobject genericType=nullptr;
    jmethodID mt=e->GetStaticMethodID(mtClass,"methodType","(Ljava/lang/Class;[Ljava/lang/Class;)Ljava/lang/invoke/MethodType;");
    jobjectArray params=e->NewObjectArray(argc,objClass,nullptr);
    genericType=e->CallStaticObjectMethod(mtClass,mt,objClass,params); e->DeleteLocalRef(params);
    jobject fallback=e->CallObjectMethod(original,asType,genericType);
    jobject constantH=e->CallStaticObjectMethod(methodHandles,constant,objClass,value);
    jobjectArray dropped=e->NewObjectArray(argc,objClass,objClass);
    jobject target=e->CallStaticObjectMethod(methodHandles,drop,constantH,0,dropped); e->DeleteLocalRef(dropped);
    if(e->ExceptionCheck()||!fallback||!target){e->ExceptionClear();return nullptr;}
    // String.equals(Object), then bind its receiver to the literal property name.
    jobjectArray eqParams=e->NewObjectArray(1,objClass,nullptr); e->SetObjectArrayElement(eqParams,0,objClass);
    jstring eqName=e->NewStringUTF("equals"); jobject eqMethod=e->CallObjectMethod(strClass,getDeclared,eqName,eqParams);
    jobject eqHandle=e->CallObjectMethod(lookup,unreflect,eqMethod);
    jobjectArray bv=e->NewObjectArray(1,objClass,nullptr); e->SetObjectArrayElement(bv,0,e->NewStringUTF("deviceUniqueId"));
    jobject bound=e->CallStaticObjectMethod(methodHandles,insert,eqHandle,0,bv);
    jfieldID typeField=e->GetStaticFieldID(boolClass,"TYPE","Ljava/lang/Class;"); jobject boolType=e->GetStaticObjectField(boolClass,typeField);
    jobjectArray testParams=e->NewObjectArray(argc,objClass,nullptr);
    jobject testType=e->CallStaticObjectMethod(mtClass,mt,boolType,testParams); e->DeleteLocalRef(testParams);
    jintArray reorder=e->NewIntArray(1); jint idx=testArgIndex; e->SetIntArrayRegion(reorder,0,1,&idx);
    jobject test=e->CallStaticObjectMethod(methodHandles,permute,bound,testType,reorder);
    jobject out=e->CallStaticObjectMethod(methodHandles,guard,test,target,fallback);
    if(e->ExceptionCheck()){e->ExceptionClear();out=nullptr;}
    return out;
}

static void installDrm(JNIEnv* e,const DeviceCfg& c) {
    if(c.drm.empty())return;
    jclass md=find(e,"android/media/MediaDrm"); jclass cc=find(e,"java/lang/Class"); jclass mh=find(e,"java/lang/invoke/MethodHandle");
    if(!md||!cc||!mh)return;
    jmethodID gm=e->GetMethodID(cc,"getDeclaredMethod","(Ljava/lang/String;[Ljava/lang/Class;)Ljava/lang/reflect/Method;"); jmethodID cb=e->GetMethodID(mh,"invokeWithArguments","([Ljava/lang/Object;)Ljava/lang/Object;");
    jobjectArray p=e->NewObjectArray(1,cc,nullptr); jclass sc=find(e,"java/lang/String"); e->SetObjectArrayElement(p,0,sc);
    jstring n=e->NewStringUTF("getPropertyByteArray"); jobject m=e->CallObjectMethod(md,gm,n,p); e->DeleteLocalRef(p);e->DeleteLocalRef(n);
    if(e->ExceptionCheck()||!m){e->ExceptionClear();return;}
    bool hex=!c.drm.empty()&&c.drm.size()%2==0&&std::all_of(c.drm.begin(),c.drm.end(),[](unsigned char x){return std::isxdigit(x);});
    jbyteArray a=e->NewByteArray(hex?(jsize)c.drm.size()/2:(jsize)c.drm.size());
    if(hex){std::vector<jbyte>b(c.drm.size()/2);auto val=[](char x){if(x>='0'&&x<='9')return x-'0';if(x>='a'&&x<='f')return x-'a'+10;return x-'A'+10;};for(size_t i=0;i<b.size();++i)b[i]=(jbyte)((val(c.drm[i*2])<<4)|val(c.drm[i*2+1]));e->SetByteArrayRegion(a,0,b.size(),b.data());}
    else e->SetByteArrayRegion(a,0,c.drm.size(),reinterpret_cast<const jbyte*>(c.drm.data()));
    jobject h=buildGuardedConstant(e,m,a,2,1); if(!h)return;
    jobject b=lsplant::Hook(e,m,h,cb); if(b){g_hookers.push_back(e->NewGlobalRef(h));LOGI("hooked MediaDrm.getPropertyByteArray(deviceUniqueId)");}
}

static void installTelephony(JNIEnv* e,const DeviceCfg& c) {
    struct X { const char* n; const std::string* v; } xs[] = {
      {"getSimOperator",&c.simOperator},{"getSimOperatorName",&c.simOperatorName},{"getSimCountryIso",&c.simCountryIso},
      {"getNetworkOperator",&c.networkOperator},{"getNetworkOperatorName",&c.networkOperatorName},
      {"getSimSerialNumber",&c.simSerial},{"getSubscriberId",&c.subscriberId},{"getLine1Number",&c.line1Number}
    };
    for(auto& x:xs) if(!x.v->empty()) { jobject v=newString(e,*x.v); hook(e,"android/telephony/TelephonyManager",x.n,"()Ljava/lang/String;",v,1); }
}

void install(JNIEnv* e) {
    if(g_installed || !e) return;
    if(!copg_lsplant::initialized() && !copg_lsplant::init(e)) return;
    DeviceCfg c=load();
    installWifi(e,c); installDrm(e,c); installTelephony(e,c);
    g_installed=true;
}
}
