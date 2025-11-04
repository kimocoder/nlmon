/* access_control.h - Role-based access control and API key management
 *
 * Provides comprehensive access control including user authentication,
 * role-based permissions, API key management, and session tracking.
 */

#ifndef ACCESS_CONTROL_H
#define ACCESS_CONTROL_H

#include <stdint.h>
#include <stdbool.h>
#include <time.h>

/* User roles */
enum user_role {
	ROLE_VIEWER = 0,      /* Read-only access */
	ROLE_OPERATOR = 1,    /* Read + filter/export */
	ROLE_ADMIN = 2        /* Full access including config */
};

/* Permissions */
enum permission {
	PERM_VIEW_EVENTS = 0x01,
	PERM_EXPORT_EVENTS = 0x02,
	PERM_MANAGE_FILTERS = 0x04,
	PERM_VIEW_CONFIG = 0x08,
	PERM_MODIFY_CONFIG = 0x10,
	PERM_MANAGE_USERS = 0x20,
	PERM_MANAGE_API_KEYS = 0x40,
	PERM_VIEW_SECURITY = 0x80
};

/* User structure */
struct ac_user {
	char username[64];
	char password_hash[128];
	enum user_role role;
	uint32_t permissions;
	bool active;
	time_t created;
	time_t last_login;
	uint32_t login_count;
};

/* Session structure */
struct ac_session {
	char session_id[64];
	char username[64];
	enum user_role role;
	uint32_t permissions;
	time_t created;
	time_t expires;
	time_t last_activity;
	char ip_address[46];  /* IPv6 max length */
	bool active;
};

/* API key structure */
struct ac_api_key {
	char key_id[64];
	char key_hash[128];
	char name[128];
	char owner[64];
	enum user_role role;
	uint32_t permissions;
	time_t created;
	time_t expires;
	bool active;
	uint32_t usage_count;
	time_t last_used;
};

/* Access control context (opaque) */
struct access_control;

/* Configuration */
struct access_control_config {
	uint32_t session_timeout;      /* Session timeout in seconds */
	uint32_t max_sessions;         /* Maximum concurrent sessions */
	uint32_t max_api_keys;         /* Maximum API keys */
	bool require_strong_passwords; /* Enforce password complexity */
	uint32_t password_min_length;  /* Minimum password length */
	bool enable_session_renewal;   /* Auto-renew sessions on activity */
	uint32_t max_login_attempts;   /* Max failed login attempts */
	uint32_t lockout_duration;     /* Account lockout duration (seconds) */
};

/**
 * access_control_create() - Create access control system
 * @config: Configuration parameters
 *
 * Returns: Pointer to access control context or NULL on error
 */
struct access_control *access_control_create(struct access_control_config *config);

/**
 * access_control_destroy() - Destroy access control system
 * @ac: Access control context
 */
void access_control_destroy(struct access_control *ac);

/* User Management */

/**
 * access_control_add_user() - Add new user
 * @ac: Access control context
 * @username: Username
 * @password: Password (will be hashed)
 * @role: User role
 *
 * Returns: 0 on success, -1 on error
 */
int access_control_add_user(struct access_control *ac,
                            const char *username,
                            const char *password,
                            enum user_role role);

/**
 * access_control_remove_user() - Remove user
 * @ac: Access control context
 * @username: Username to remove
 *
 * Returns: 0 on success, -1 on error
 */
int access_control_remove_user(struct access_control *ac, const char *username);

/**
 * access_control_update_password() - Update user password
 * @ac: Access control context
 * @username: Username
 * @old_password: Current password
 * @new_password: New password
 *
 * Returns: 0 on success, -1 on error
 */
int access_control_update_password(struct access_control *ac,
                                   const char *username,
                                   const char *old_password,
                                   const char *new_password);

/**
 * access_control_set_user_role() - Update user role
 * @ac: Access control context
 * @username: Username
 * @role: New role
 *
 * Returns: 0 on success, -1 on error
 */
int access_control_set_user_role(struct access_control *ac,
                                 const char *username,
                                 enum user_role role);

/* Authentication */

/**
 * access_control_login() - Authenticate user and create session
 * @ac: Access control context
 * @username: Username
 * @password: Password
 * @ip_address: Client IP address (optional)
 * @session_id: Output buffer for session ID
 * @session_id_len: Size of session_id buffer
 *
 * Returns: 0 on success, -1 on error
 */
int access_control_login(struct access_control *ac,
                        const char *username,
                        const char *password,
                        const char *ip_address,
                        char *session_id,
                        size_t session_id_len);

/**
 * access_control_logout() - Terminate session
 * @ac: Access control context
 * @session_id: Session ID
 *
 * Returns: 0 on success, -1 on error
 */
int access_control_logout(struct access_control *ac, const char *session_id);

/**
 * access_control_validate_session() - Validate session
 * @ac: Access control context
 * @session_id: Session ID
 * @username: Output buffer for username (optional)
 * @username_len: Size of username buffer
 *
 * Returns: 0 if valid, -1 if invalid or expired
 */
int access_control_validate_session(struct access_control *ac,
                                    const char *session_id,
                                    char *username,
                                    size_t username_len);

/* Authorization */

/**
 * access_control_check_permission() - Check if session has permission
 * @ac: Access control context
 * @session_id: Session ID
 * @permission: Permission to check
 *
 * Returns: true if authorized, false otherwise
 */
bool access_control_check_permission(struct access_control *ac,
                                     const char *session_id,
                                     enum permission permission);

/**
 * access_control_get_role() - Get role for session
 * @ac: Access control context
 * @session_id: Session ID
 *
 * Returns: User role or -1 on error
 */
int access_control_get_role(struct access_control *ac, const char *session_id);

/* API Key Management */

/**
 * access_control_create_api_key() - Create new API key
 * @ac: Access control context
 * @name: Descriptive name for the key
 * @owner: Username of key owner
 * @role: Role for the key
 * @expires_in: Expiration time in seconds (0 = never)
 * @key: Output buffer for generated key
 * @key_len: Size of key buffer
 *
 * Returns: 0 on success, -1 on error
 */
int access_control_create_api_key(struct access_control *ac,
                                  const char *name,
                                  const char *owner,
                                  enum user_role role,
                                  uint32_t expires_in,
                                  char *key,
                                  size_t key_len);

/**
 * access_control_revoke_api_key() - Revoke API key
 * @ac: Access control context
 * @key_id: Key ID to revoke
 *
 * Returns: 0 on success, -1 on error
 */
int access_control_revoke_api_key(struct access_control *ac, const char *key_id);

/**
 * access_control_validate_api_key() - Validate API key
 * @ac: Access control context
 * @key: API key
 * @owner: Output buffer for key owner (optional)
 * @owner_len: Size of owner buffer
 *
 * Returns: 0 if valid, -1 if invalid or expired
 */
int access_control_validate_api_key(struct access_control *ac,
                                    const char *key,
                                    char *owner,
                                    size_t owner_len);

/**
 * access_control_check_api_key_permission() - Check API key permission
 * @ac: Access control context
 * @key: API key
 * @permission: Permission to check
 *
 * Returns: true if authorized, false otherwise
 */
bool access_control_check_api_key_permission(struct access_control *ac,
                                             const char *key,
                                             enum permission permission);

/* Session Management */

/**
 * access_control_list_sessions() - List active sessions
 * @ac: Access control context
 * @sessions: Output array for sessions
 * @max_sessions: Maximum number of sessions to return
 *
 * Returns: Number of sessions returned
 */
int access_control_list_sessions(struct access_control *ac,
                                 struct ac_session *sessions,
                                 size_t max_sessions);

/**
 * access_control_terminate_user_sessions() - Terminate all sessions for user
 * @ac: Access control context
 * @username: Username
 *
 * Returns: Number of sessions terminated
 */
int access_control_terminate_user_sessions(struct access_control *ac,
                                           const char *username);

/**
 * access_control_cleanup_expired() - Remove expired sessions and keys
 * @ac: Access control context
 *
 * Returns: Number of items cleaned up
 */
int access_control_cleanup_expired(struct access_control *ac);

/* Statistics */

/**
 * access_control_stats() - Get access control statistics
 * @ac: Access control context
 * @total_users: Output for total users
 * @active_sessions: Output for active sessions
 * @total_api_keys: Output for total API keys
 * @failed_logins: Output for failed login attempts
 */
void access_control_stats(struct access_control *ac,
                         size_t *total_users,
                         size_t *active_sessions,
                         size_t *total_api_keys,
                         unsigned long *failed_logins);

/* Helper functions */

/**
 * access_control_role_string() - Get string representation of role
 * @role: User role
 *
 * Returns: String representation
 */
const char *access_control_role_string(enum user_role role);

/**
 * access_control_permission_string() - Get string representation of permission
 * @permission: Permission
 *
 * Returns: String representation
 */
const char *access_control_permission_string(enum permission permission);

/**
 * access_control_get_role_permissions() - Get default permissions for role
 * @role: User role
 *
 * Returns: Permission bitmask
 */
uint32_t access_control_get_role_permissions(enum user_role role);

#endif /* ACCESS_CONTROL_H */
