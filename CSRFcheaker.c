#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <gumbo.h>

// 保存网页内容的结构体
struct MemoryStruct {
    char *memory;
    size_t size;
};

// 回调函数，用于处理 libcurl 的数据流
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (ptr == NULL) {
        fprintf(stderr, "[-] 内存分配失败\n");
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

// 解析HTML并寻找表单及CSRF token
void search_for_csrf_token(GumboNode *node) {
    if (node->type != GUMBO_NODE_ELEMENT) return;

    GumboAttribute *method_attr = NULL;
    if (node->v.element.tag == GUMBO_TAG_FORM) {
        method_attr = gumbo_get_attribute(&node->v.element.attributes, "method");
        if (method_attr && strcasecmp(method_attr->value, "post") == 0) {
            printf("[+] 发现一个 POST 表单\n");

            // 查找表单中的 input 元素
            GumboVector *children = &node->v.element.children;
            for (unsigned int i = 0; i < children->length; ++i) {
                GumboNode *child = (GumboNode *)children->data[i];
                if (child->type == GUMBO_NODE_ELEMENT && child->v.element.tag == GUMBO_TAG_INPUT) {
                    GumboAttribute *name_attr = gumbo_get_attribute(&child->v.element.attributes, "name");
                    if (name_attr && strstr(name_attr->value, "csrf") != NULL) {
                        printf("[+] 发现 CSRF 令牌字段: %s\n", name_attr->value);
                        return;
                    }
                }
            }
            printf("[-] 此表单中未找到 CSRF 令牌\n");
        }
    }

    // 递归遍历子节点
    GumboVector *children = &node->v.element.children;
    for (unsigned int i = 0; i < children->length; ++i) {
        search_for_csrf_token((GumboNode *)children->data[i]);
    }
}

int main(void) {
    CURL *curl_handle;
    CURLcode res;

    struct MemoryStruct chunk;

    chunk.memory = malloc(1);  // 初始化
    chunk.size = 0;            // 没有内容

    // 初始化 libcurl
    curl_global_init(CURL_GLOBAL_ALL);
    curl_handle = curl_easy_init();

    if (!curl_handle) {
        fprintf(stderr, "[-] 初始化 libcurl 失败\n");
        free(chunk.memory);
        return 1;
    }

    // 输入目标 URL
    char url[256];
    printf("请输入目标 URL: ");
    if (fgets(url, sizeof(url), stdin) == NULL) {
        fprintf(stderr, "[-] 读取 URL 输入失败\n");
        curl_easy_cleanup(curl_handle);
        free(chunk.memory);
        curl_global_cleanup();
        return 1;
    }

    // 去除输入中的换行符
    url[strcspn(url, "\n")] = '\0';

    // 设置 libcurl 选项
    curl_easy_setopt(curl_handle, CURLOPT_URL, url);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
    curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);

    // 禁用 SSL 证书验证（仅限测试环境）
    curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYHOST, 0L);

    // 执行请求
    res = curl_easy_perform(curl_handle);

    // 检查请求是否成功
    if (res != CURLE_OK) {
        fprintf(stderr, "[-] curl_easy_perform() 失败: %s\n", curl_easy_strerror(res));
    } else {
        printf("[+] 成功抓取网页内容，大小: %lu 字节\n", (unsigned long)chunk.size);

        // 打印抓取到的 HTML 内容（前 500 字节）
        printf("[+] 抓取的网页内容（前 500 字节）:\n");
        size_t print_size = chunk.size < 500 ? chunk.size : 500;
        fwrite(chunk.memory, 1, print_size, stdout);
        printf("\n");

        // 解析 HTML 内容
        GumboOutput *output = gumbo_parse(chunk.memory);
        search_for_csrf_token(output->root);
        gumbo_destroy_output(&kGumboDefaultOptions, output);
    }

    // 清理
    curl_easy_cleanup(curl_handle);
    free(chunk.memory);
    curl_global_cleanup();

    return 0;
}
