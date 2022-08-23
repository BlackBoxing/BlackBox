//
// Created by Milk on 4/10/21.
//

#include <sys/mman.h>
#include <bits/sysconf.h>
#include "IO.h"
#include "Log.h"
#include "JniHook/JniHook.h"
#include "dobby.h"

jmethodID getAbsolutePathMethodId;
list<IO::RelocateInfo> relocate_rule;
list<const char *> white_rule;

char *replace(const char *str, const char *src, const char *dst) {
    const char *pos = str;
    int count = 0;
    while ((pos = strstr(pos, src))) {
        count++;
        pos += strlen(src);
    }

    size_t result_len = strlen(str) + (strlen(dst) - strlen(src)) * count + 1;
    char *result = (char *) malloc(result_len);
    memset(result, 0, strlen(result));

    const char *left = str;
    const char *right = nullptr;

    while ((right = strstr(left, src))) {
        strncat(result, left, right - left);
        strcat(result, dst);
        right += strlen(src);
        left = right;
    }
    strcat(result, left);
    return result;
}

const char *IO::redirectPath(const char *__path) {
    list<IO::RelocateInfo>::iterator iterator;
    for (iterator = relocate_rule.begin(); iterator != relocate_rule.end(); ++iterator) {
        IO::RelocateInfo info = *iterator;
        if (strstr(__path, info.targetPath) && !strstr(__path, "/blackbox/")) {
            char *ret = replace(__path, info.targetPath, info.relocatePath);
            //ALOGD("redirectPath %s  => %s", __path, ret);
            return ret;
        }
    }
    return __path;
}

//#ifdef __arm__ https://developer.android.com/games/optimize/64-bit?hl=zh-cn
HOOK_JNI(void *, openat, int fd, const char *pathname, int flags, int mode) {
    // 执行 stack 清理（不可省略），只需调用一次
    // SHADOWHOOK_STACK_SCOPE();
    list<const char *>::iterator white_iterator;
    for (white_iterator = white_rule.begin();
         white_iterator != white_rule.end(); ++white_iterator) {
        const char *info = *white_iterator;
        if (strstr(pathname, info)) {
            return orig_openat(fd, pathname, flags, mode);
        }
    }
    list<IO::RelocateInfo>::iterator iterator;
    for (iterator = relocate_rule.begin(); iterator != relocate_rule.end(); ++iterator) {
        IO::RelocateInfo info = *iterator;
        if (strstr(pathname, info.targetPath) && !strstr(pathname, "/blackbox/")) {
            log_print_debug("redirectPath %s  => %s", pathname, info.relocatePath);
            return orig_openat(fd, info.relocatePath, flags, mode);
        }
    }
    // 调用原函数
    return orig_openat(fd, pathname, flags, mode);
}

HOOK_JNI(FILE *, popen, const char* cmd, const char* mode) {
    // 执行 stack 清理（不可省略），只需调用一次
    // SHADOWHOOK_STACK_SCOPE();

    // 调用原函数
    return orig_popen(cmd, mode);
}
//#endif
jstring IO::redirectPath(JNIEnv *env, jstring path) {
    /*const char * pathC = env->GetStringUTFChars(path, JNI_FALSE);
    const char *redirect = redirectPath(pathC);
    env->ReleaseStringUTFChars(path, pathC);
    return env->NewStringUTF(redirect);*/
    return BoxCore::redirectPathString(env, path);
}

jobject IO::redirectPath(JNIEnv *env, jobject path) {
    /*auto pathStr =
            reinterpret_cast<jstring>(env->CallObjectMethod(path, getAbsolutePathMethodId));
    jstring redirect = redirectPath(env, pathStr);
    jobject file = env->NewObject(fileClazz, fileNew, redirect);
    env->DeleteLocalRef(pathStr);
    env->DeleteLocalRef(redirect);*/
    return BoxCore::redirectPathFile(env, path);
}

void IO::addWhiteList(const char *path) {
    white_rule.push_back(path);
}

void IO::addRule(const char *targetPath, const char *relocatePath) {
    IO::RelocateInfo info{};
    info.targetPath = targetPath;
    info.relocatePath = relocatePath;
    relocate_rule.push_back(info);
}

void IO::unProtect(const char *libraryName, const char *symbol) {
    auto pageSize = sysconf(_SC_PAGE_SIZE);
    auto symbolAddress = ((uintptr_t) DobbySymbolResolver(libraryName, symbol)) & (-pageSize);
    mprotect((void *) symbolAddress, pageSize, PROT_READ | PROT_WRITE | PROT_EXEC);
}

void IO::init(JNIEnv *env) {
    jclass tmpFile = env->FindClass("java/io/File");
    getAbsolutePathMethodId = env->GetMethodID(tmpFile, "getAbsolutePath", "()Ljava/lang/String;");
    //shadowhook_hook_sym_name("libc.so", "open", (void *) shared_proxy_read, NULL);

    IO::unProtect("libc.so", "openat");
    void *openatAddress = DobbySymbolResolver("libc.so", "openat");
    if (openatAddress) {
        DobbyHook(openatAddress, (void *) new_openat, (void **) &orig_openat);
    }

    IO::unProtect("libc.so", "popen");
    void *popenAddress = DobbySymbolResolver("libc.so", "popen");
    if (popenAddress) {
        DobbyHook(popenAddress, (void *) new_popen, (void **) &orig_popen);
    }
}
