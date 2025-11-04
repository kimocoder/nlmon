#ifndef WEB_AUTH_H
#define WEB_AUTH_H

#include <stdint.h>
#include <time.h>

/* User structure */
struct web_user {
    char username[64];
    char password_hash[128];
    char role[32];
    int active;
};

/* Session structure */
struct web_session {
    char token[256];
    char username[64];
    time_t created;
    time_t expires;
    int active;
};

/* Authentication context */
struct web_auth_context;

/* Initialize authentication system */
struct web_auth_context *web_auth_init(const char *secret_key);

/* Cleanup authentication system */
void web_auth_cleanup(struct web_auth_context *ctx);

/* Add user */
int web_auth_add_user(struct web_auth_context *ctx, const char *username,
                      const char *password, const char *role);

/* Authenticate user */
char *web_auth_login(struct web_auth_context *ctx, const char *username,
                     const char *password);

/* Validate token */
int web_auth_validate_token(struct web_auth_context *ctx, const char *token,
                            char *username, size_t username_len);

/* Logout */
int web_auth_logout(struct web_auth_context *ctx, const char *token);

/* Generate JWT token */
char *web_auth_generate_jwt(struct web_auth_context *ctx, const char *username,
                            const char *role, int expires_in);

/* Verify JWT token */
int web_auth_verify_jwt(struct web_auth_context *ctx, const char *token,
                        char *username, size_t username_len,
                        char *role, size_t role_len);

/* Hash password */
int web_auth_hash_password(const char *password, char *hash, size_t hash_len);

/* Verify password */
int web_auth_verify_password(const char *password, const char *hash);

#endif /* WEB_AUTH_H */
