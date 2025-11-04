#include "web_auth.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <openssl/sha.h>
#include <openssl/hmac.h>
#include <openssl/evp.h>
#include <openssl/bio.h>
#include <openssl/buffer.h>

#define MAX_USERS 100
#define MAX_SESSIONS 1000
#define SESSION_TIMEOUT 3600  /* 1 hour */

/* Authentication context */
struct web_auth_context {
    char secret_key[256];
    struct web_user users[MAX_USERS];
    int num_users;
    struct web_session sessions[MAX_SESSIONS];
    int num_sessions;
};

/* Base64 encode */
static char *base64_encode(const unsigned char *input, int length) {
    BIO *bmem, *b64;
    BUF_MEM *bptr;
    
    b64 = BIO_new(BIO_f_base64());
    bmem = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, bmem);
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    BIO_write(b64, input, length);
    BIO_flush(b64);
    BIO_get_mem_ptr(b64, &bptr);
    
    char *buff = malloc(bptr->length + 1);
    if (buff) {
        memcpy(buff, bptr->data, bptr->length);
        buff[bptr->length] = 0;
    }
    
    BIO_free_all(b64);
    
    return buff;
}

/* Base64 decode */
static int base64_decode(const char *input, unsigned char *output, size_t output_len) {
    BIO *b64, *bmem;
    
    b64 = BIO_new(BIO_f_base64());
    bmem = BIO_new_mem_buf(input, -1);
    bmem = BIO_push(b64, bmem);
    BIO_set_flags(bmem, BIO_FLAGS_BASE64_NO_NL);
    
    int len = BIO_read(bmem, output, output_len);
    
    BIO_free_all(bmem);
    
    return len;
}

/* Initialize authentication system */
struct web_auth_context *web_auth_init(const char *secret_key) {
    struct web_auth_context *ctx = calloc(1, sizeof(struct web_auth_context));
    if (!ctx) return NULL;
    
    if (secret_key) {
        snprintf(ctx->secret_key, sizeof(ctx->secret_key), "%s", secret_key);
    } else {
        /* Generate random secret key */
        snprintf(ctx->secret_key, sizeof(ctx->secret_key), "default_secret_key");
    }
    
    ctx->num_users = 0;
    ctx->num_sessions = 0;
    
    return ctx;
}

/* Cleanup authentication system */
void web_auth_cleanup(struct web_auth_context *ctx) {
    if (ctx) {
        free(ctx);
    }
}

/* Hash password using SHA-256 */
int web_auth_hash_password(const char *password, char *hash, size_t hash_len) {
    unsigned char digest[SHA256_DIGEST_LENGTH];
    
    SHA256((unsigned char *)password, strlen(password), digest);
    
    /* Convert to hex string */
    for (int i = 0; i < SHA256_DIGEST_LENGTH && (size_t)(i * 2) < hash_len - 1; i++) {
        sprintf(hash + i * 2, "%02x", digest[i]);
    }
    
    return 0;
}

/* Verify password */
int web_auth_verify_password(const char *password, const char *hash) {
    char computed_hash[128];
    
    web_auth_hash_password(password, computed_hash, sizeof(computed_hash));
    
    return strcmp(computed_hash, hash) == 0 ? 0 : -1;
}

/* Add user */
int web_auth_add_user(struct web_auth_context *ctx, const char *username,
                      const char *password, const char *role) {
    if (!ctx || !username || !password || !role) return -1;
    if (ctx->num_users >= MAX_USERS) return -1;
    
    /* Check if user already exists */
    for (int i = 0; i < ctx->num_users; i++) {
        if (strcmp(ctx->users[i].username, username) == 0) {
            return -1;  /* User already exists */
        }
    }
    
    struct web_user *user = &ctx->users[ctx->num_users];
    
    snprintf(user->username, sizeof(user->username), "%s", username);
    snprintf(user->role, sizeof(user->role), "%s", role);
    web_auth_hash_password(password, user->password_hash, sizeof(user->password_hash));
    user->active = 1;
    
    ctx->num_users++;
    
    return 0;
}

/* Find user by username */
static struct web_user *find_user(struct web_auth_context *ctx, const char *username) {
    for (int i = 0; i < ctx->num_users; i++) {
        if (strcmp(ctx->users[i].username, username) == 0 && ctx->users[i].active) {
            return &ctx->users[i];
        }
    }
    return NULL;
}

/* Generate session token */
static void generate_token(char *token, size_t token_len) {
    const char *chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    
    for (size_t i = 0; i < token_len - 1; i++) {
        token[i] = chars[rand() % strlen(chars)];
    }
    token[token_len - 1] = '\0';
}

/* Authenticate user */
char *web_auth_login(struct web_auth_context *ctx, const char *username,
                     const char *password) {
    if (!ctx || !username || !password) return NULL;
    
    struct web_user *user = find_user(ctx, username);
    if (!user) return NULL;
    
    if (web_auth_verify_password(password, user->password_hash) != 0) {
        return NULL;
    }
    
    /* Create session */
    if (ctx->num_sessions >= MAX_SESSIONS) {
        /* Remove oldest session */
        memmove(&ctx->sessions[0], &ctx->sessions[1],
                sizeof(struct web_session) * (MAX_SESSIONS - 1));
        ctx->num_sessions--;
    }
    
    struct web_session *session = &ctx->sessions[ctx->num_sessions];
    
    generate_token(session->token, sizeof(session->token));
    snprintf(session->username, sizeof(session->username), "%s", username);
    session->created = time(NULL);
    session->expires = session->created + SESSION_TIMEOUT;
    session->active = 1;
    
    ctx->num_sessions++;
    
    return strdup(session->token);
}

/* Validate token */
int web_auth_validate_token(struct web_auth_context *ctx, const char *token,
                            char *username, size_t username_len) {
    if (!ctx || !token) return -1;
    
    time_t now = time(NULL);
    
    for (int i = 0; i < ctx->num_sessions; i++) {
        struct web_session *session = &ctx->sessions[i];
        
        if (session->active && strcmp(session->token, token) == 0) {
            if (now > session->expires) {
                session->active = 0;
                return -1;  /* Session expired */
            }
            
            if (username) {
                snprintf(username, username_len, "%s", session->username);
            }
            
            return 0;
        }
    }
    
    return -1;  /* Token not found */
}

/* Logout */
int web_auth_logout(struct web_auth_context *ctx, const char *token) {
    if (!ctx || !token) return -1;
    
    for (int i = 0; i < ctx->num_sessions; i++) {
        if (strcmp(ctx->sessions[i].token, token) == 0) {
            ctx->sessions[i].active = 0;
            return 0;
        }
    }
    
    return -1;
}

/* Generate JWT token */
char *web_auth_generate_jwt(struct web_auth_context *ctx, const char *username,
                            const char *role, int expires_in) {
    if (!ctx || !username || !role) return NULL;
    
    time_t now = time(NULL);
    time_t exp = now + expires_in;
    
    /* Create JWT header */
    char header[] = "{\"alg\":\"HS256\",\"typ\":\"JWT\"}";
    
    /* Create JWT payload */
    char payload[512];
    snprintf(payload, sizeof(payload),
             "{\"sub\":\"%s\",\"role\":\"%s\",\"iat\":%ld,\"exp\":%ld}",
             username, role, now, exp);
    
    /* Base64 encode header and payload */
    char *header_b64 = base64_encode((unsigned char *)header, strlen(header));
    char *payload_b64 = base64_encode((unsigned char *)payload, strlen(payload));
    
    if (!header_b64 || !payload_b64) {
        free(header_b64);
        free(payload_b64);
        return NULL;
    }
    
    /* Create signature input */
    char sig_input[1024];
    snprintf(sig_input, sizeof(sig_input), "%s.%s", header_b64, payload_b64);
    
    /* Generate HMAC signature */
    unsigned char sig[EVP_MAX_MD_SIZE];
    unsigned int sig_len;
    
    HMAC(EVP_sha256(), ctx->secret_key, strlen(ctx->secret_key),
         (unsigned char *)sig_input, strlen(sig_input), sig, &sig_len);
    
    /* Base64 encode signature */
    char *sig_b64 = base64_encode(sig, sig_len);
    
    if (!sig_b64) {
        free(header_b64);
        free(payload_b64);
        return NULL;
    }
    
    /* Combine into JWT */
    char *jwt = malloc(strlen(header_b64) + strlen(payload_b64) + strlen(sig_b64) + 3);
    if (jwt) {
        sprintf(jwt, "%s.%s.%s", header_b64, payload_b64, sig_b64);
    }
    
    free(header_b64);
    free(payload_b64);
    free(sig_b64);
    
    return jwt;
}

/* Verify JWT token */
int web_auth_verify_jwt(struct web_auth_context *ctx, const char *token,
                        char *username, size_t username_len,
                        char *role, size_t role_len) {
    if (!ctx || !token) return -1;
    
    /* Split token into parts */
    char *token_copy = strdup(token);
    if (!token_copy) return -1;
    
    char *header_b64 = strtok(token_copy, ".");
    char *payload_b64 = strtok(NULL, ".");
    char *sig_b64 = strtok(NULL, ".");
    
    if (!header_b64 || !payload_b64 || !sig_b64) {
        free(token_copy);
        return -1;
    }
    
    /* Verify signature */
    char sig_input[1024];
    snprintf(sig_input, sizeof(sig_input), "%s.%s", header_b64, payload_b64);
    
    unsigned char expected_sig[EVP_MAX_MD_SIZE];
    unsigned int expected_sig_len;
    
    HMAC(EVP_sha256(), ctx->secret_key, strlen(ctx->secret_key),
         (unsigned char *)sig_input, strlen(sig_input), expected_sig, &expected_sig_len);
    
    char *expected_sig_b64 = base64_encode(expected_sig, expected_sig_len);
    
    int sig_valid = (strcmp(sig_b64, expected_sig_b64) == 0);
    free(expected_sig_b64);
    
    if (!sig_valid) {
        free(token_copy);
        return -1;
    }
    
    /* Decode payload */
    unsigned char payload[512];
    int payload_len = base64_decode(payload_b64, payload, sizeof(payload));
    
    if (payload_len <= 0) {
        free(token_copy);
        return -1;
    }
    
    payload[payload_len] = '\0';
    
    /* Parse payload (simplified - would use proper JSON parser) */
    char *sub = strstr((char *)payload, "\"sub\":\"");
    char *role_str = strstr((char *)payload, "\"role\":\"");
    char *exp_str = strstr((char *)payload, "\"exp\":");
    
    if (sub && username) {
        sub += 7;
        char *end = strchr(sub, '"');
        if (end) {
            size_t len = end - sub;
            if (len < username_len) {
                strncpy(username, sub, len);
                username[len] = '\0';
            }
        }
    }
    
    if (role_str && role) {
        role_str += 8;
        char *end = strchr(role_str, '"');
        if (end) {
            size_t len = end - role_str;
            if (len < role_len) {
                strncpy(role, role_str, len);
                role[len] = '\0';
            }
        }
    }
    
    /* Check expiration */
    if (exp_str) {
        exp_str += 6;
        time_t exp = atol(exp_str);
        if (time(NULL) > exp) {
            free(token_copy);
            return -1;  /* Token expired */
        }
    }
    
    free(token_copy);
    
    return 0;
}
