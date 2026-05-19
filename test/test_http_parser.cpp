// 让私有成员在本翻译单元内可直接访问，不改动生产代码
// 注意：#define private public 只影响访问控制，不改变类内存布局，链接安全。
#define private public
#include "http/http_conn.h"
#undef private

#include <gtest/gtest.h>
#include <cstring>

// ══════════════════════════════════════════════════════════════════
// 测试夹具：每个用例得到一个干净的 http_conn 解析状态
// ══════════════════════════════════════════════════════════════════
class HttpParserTest : public ::testing::Test {
protected:
    http_conn conn;

    void SetUp() override {
        // 必须先设置 m_close_log，否则 LOG_* 宏会尝试访问未初始化的 Log 单例
        conn.m_close_log = 1;
        conn.m_sockfd    = -1;
        conn.m_TRIGMode  = 0;
        conn.doc_root    = nullptr;
        conn.mysql       = nullptr;
        // 调用私有 init()：重置所有解析状态，不涉及 epoll/socket
        conn.init();
    }

    // 把原始 HTTP 报文写入读缓冲区
    void LoadRequest(const char *raw) {
        size_t n = strlen(raw);
        memcpy(conn.m_read_buf, raw, n);
        conn.m_read_idx = static_cast<long>(n);
    }
};

// ══════════════════════════════════════════════════════════════════
// parse_line：从状态机中提取一行
// ══════════════════════════════════════════════════════════════════

TEST_F(HttpParserTest, ParseLine_CompleteCRLF_ReturnsOK) {
    LoadRequest("GET / HTTP/1.1\r\n");
    EXPECT_EQ(conn.parse_line(), http_conn::LINE_OK);
    // parse_line 会把 \r\n 替换为 \0\0
    EXPECT_EQ(conn.m_read_buf[14], '\0');
    EXPECT_EQ(conn.m_read_buf[15], '\0');
    EXPECT_EQ(conn.m_checked_idx, 16);
}

TEST_F(HttpParserTest, ParseLine_IncompleteData_ReturnsOpen) {
    // 只有 \r，\n 尚未到达——模拟数据未读完
    LoadRequest("GET / HTTP/1.1\r");
    EXPECT_EQ(conn.parse_line(), http_conn::LINE_OPEN);
}

TEST_F(HttpParserTest, ParseLine_StrayLF_ReturnsBad) {
    // \n 前没有 \r，协议格式错误
    LoadRequest("GET\n");
    EXPECT_EQ(conn.parse_line(), http_conn::LINE_BAD);
}

TEST_F(HttpParserTest, ParseLine_EmptyLine_CRLF_ReturnsOK) {
    LoadRequest("\r\n");
    EXPECT_EQ(conn.parse_line(), http_conn::LINE_OK);
}

// ══════════════════════════════════════════════════════════════════
// parse_request_line：解析请求行
// ══════════════════════════════════════════════════════════════════

TEST_F(HttpParserTest, ParseRequestLine_GET_ParsesCorrectly) {
    char line[] = "GET / HTTP/1.1";
    EXPECT_EQ(conn.parse_request_line(line), http_conn::NO_REQUEST);
    EXPECT_EQ(conn.m_method, http_conn::GET);
    EXPECT_EQ(conn.cgi, 0);
    // 解析成功后状态机应切换到 CHECK_STATE_HEADER
    EXPECT_EQ(conn.m_check_state, http_conn::CHECK_STATE_HEADER);
}

TEST_F(HttpParserTest, ParseRequestLine_POST_SetsCGIFlag) {
    char line[] = "POST /3 HTTP/1.1";
    EXPECT_EQ(conn.parse_request_line(line), http_conn::NO_REQUEST);
    EXPECT_EQ(conn.m_method, http_conn::POST);
    EXPECT_EQ(conn.cgi, 1); // POST 必须启用 CGI 标志
}

TEST_F(HttpParserTest, ParseRequestLine_MissingURL_ReturnsBadRequest) {
    char line[] = "GET";
    EXPECT_EQ(conn.parse_request_line(line), http_conn::BAD_REQUEST);
}

TEST_F(HttpParserTest, ParseRequestLine_HTTP10_ReturnsBadRequest) {
    // 服务器仅接受 HTTP/1.1（源码硬判断）
    char line[] = "GET / HTTP/1.0";
    EXPECT_EQ(conn.parse_request_line(line), http_conn::BAD_REQUEST);
}

TEST_F(HttpParserTest, ParseRequestLine_MissingVersion_ReturnsBadRequest) {
    char line[] = "GET /";
    EXPECT_EQ(conn.parse_request_line(line), http_conn::BAD_REQUEST);
}

// ══════════════════════════════════════════════════════════════════
// parse_headers：解析请求头
// ══════════════════════════════════════════════════════════════════

TEST_F(HttpParserTest, ParseHeaders_ContentLength_Parsed) {
    char line[] = "Content-length: 42";
    conn.parse_headers(line);
    EXPECT_EQ(conn.m_content_length, 42);
}

TEST_F(HttpParserTest, ParseHeaders_KeepAlive_SetsLinger) {
    char line[] = "Connection: keep-alive";
    conn.parse_headers(line);
    EXPECT_TRUE(conn.m_linger);
}

TEST_F(HttpParserTest, ParseHeaders_Close_DoesNotSetLinger) {
    char line[] = "Connection: close";
    conn.parse_headers(line);
    EXPECT_FALSE(conn.m_linger);
}

TEST_F(HttpParserTest, ParseHeaders_EmptyLine_NoBody_ReturnsGetRequest) {
    // 空行且 content_length==0 → 头部结束，可以处理请求
    char line[] = "";
    EXPECT_EQ(conn.parse_headers(line), http_conn::GET_REQUEST);
}

TEST_F(HttpParserTest, ParseHeaders_EmptyLine_WithBody_SwitchesToContent) {
    conn.m_content_length = 10;
    char line[] = "";
    // 有请求体时应切换到 CHECK_STATE_CONTENT，而非直接 GET_REQUEST
    EXPECT_EQ(conn.parse_headers(line), http_conn::NO_REQUEST);
    EXPECT_EQ(conn.m_check_state, http_conn::CHECK_STATE_CONTENT);
}

// ══════════════════════════════════════════════════════════════════
// process_read：完整解析流水线（只测试不需要文件系统的错误路径）
// ══════════════════════════════════════════════════════════════════

TEST_F(HttpParserTest, ProcessRead_UnsupportedMethod_ReturnsBadRequest) {
    // 服务器只处理 GET/POST，DELETE 应返回 BAD_REQUEST
    LoadRequest("DELETE / HTTP/1.1\r\nHost: localhost\r\n\r\n");
    EXPECT_EQ(conn.process_read(), http_conn::BAD_REQUEST);
}

TEST_F(HttpParserTest, ProcessRead_HTTP10_ReturnsBadRequest) {
    LoadRequest("GET / HTTP/1.0\r\nHost: localhost\r\n\r\n");
    EXPECT_EQ(conn.process_read(), http_conn::BAD_REQUEST);
}

TEST_F(HttpParserTest, ProcessRead_NoURL_ReturnsBadRequest) {
    LoadRequest("GET HTTP/1.1\r\n\r\n");
    EXPECT_EQ(conn.process_read(), http_conn::BAD_REQUEST);
}

TEST_F(HttpParserTest, ProcessRead_EmptyBuffer_ReturnsNoRequest) {
    // 缓冲区为空时，parse_line 返回 LINE_OPEN，整体返回 NO_REQUEST
    EXPECT_EQ(conn.process_read(), http_conn::NO_REQUEST);
}
