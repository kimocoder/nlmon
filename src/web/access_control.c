/* access_control.c - Role-based access control implementation
 *
 * Implements user authentication, RBAC, API key management, and session tracking.
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <openssl/sha.h>
#include <openssl/rand.h>
#include "access_control.h"

#define MAX_USERS 100
#define MAX_SESSIONS 1000
#define MAX_API_KEYS 100
#define DEFAULT_SESSION_TIMEOUT 3600
#define DEFAULT_PASSWORD_MIN_LENGTH 8

/* Login attempt tracking */
struct login_attempt {
	char username[64];
	uint32_t attempts;
	time_t lockout_until;
};

/* Access control context */
struct access_control {
	struct access_control_config config;
	
	/* Users */
	struct ac_user users[MAX_USERS];
	size_t num_users;
	pthread_mutex_t users_mutex;
	
	/* Sessions */
	struct ac_session sessions[MAX_SESSIONS];
	size_t num_sessions;
	pthread_mutex_t sessions_mutex;
	
	/* API keys */
	struct ac_api_key api_keys[MAX_API_KEYS];
	size_t num_api_keys;
	pthread_mutex_t api_keys_mutex;
	
	/* Login attempts */
	struct login_attempt login_attempts[MAX_USERS];
	size_t num_login_attempts;
	pthread_mutex_t login_attempts_mutex;
	
	/* Statistics */
	atomic_ulong failed_logins;
	atomic_ulong successful_logins;
};

/* Hash password using SHA-256 */
static void hash_password(const char *password, char *hash, size_t hash_len)
{
	unsigned char digest[SHA256_DIGEST_LENGTH];
	
	SHA256((unsigned char *)password, strlen(password), digest);
	
	for (int i = 0; i < SHA256_DIGEST_LENGTH && (size_t)(i * 2) < hash_len - 1; i++)
		sprintf(hash + i * 2, "%02x", digest[i]);
}

/* Generate random string */
static void generate_random_string(char *str, size_t len)
{
	const char *chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
	unsigned char rand_bytes[len];
	
	RAND_bytes(rand_bytes, len);
	
	for (size_t i = 0; i < len - 1; i++)
		str[i] = chars[rand_bytes[i] % strlen(chars)];
	
	str[len - 1] = '\0';
}

/* Validate password strength */
static bool validate_password_strength(const char *password, uint32_t min_length)
{
	size_t len = strlen(password);
	bool has_upper = false, has_lower = false, has_digit = false;
	
	if (len < min_length)
		return false;
	
	for (size_t i = 0; i < len; i++) {
		if (password[i] >= 'A' && password[i] <= 'Z')
			has_upper = true;
		else if (password[i] >= 'a' && password[i] <= 'z')
			has_lower = true;
		else if (password[i] >= '0' && password[i] <= '9')
			has_digit = true;
	}
	
	return has_upper && has_lower && has_digit;
}

uint32_t access_control_get_role_permissions(enum user_role role)
{
	switch (role) {
	case ROLE_VIEWER:
		return PERM_VIEW_EVENTS;
	case ROLE_OPERATOR:
		return PERM_VIEW_EVENTS | PERM_EXPORT_EVENTS | 
		       PERM_MANAGE_FILTERS | PERM_VIEW_CONFIG;
	case ROLE_ADMIN:
		return PERM_VIEW_EVENTS | PERM_EXPORT_EVENTS | 
		       PERM_MANAGE_FILTERS | PERM_VIEW_CONFIG |
		       PERM_MODIFY_CONFIG | PERM_MANAGE_USERS |
		       PERM_MANAGE_API_KEYS | PERM_VIEW_SECURITY;
	default:
		return 0;
	}
}

struct access_control *access_control_create(struct access_control_config *config)
{
	struct access_control *ac;
	
	ac = calloc(1, sizeof(*ac));
	if (!ac)
		return NULL;
	
	if (config)
		ac->config = *config;
	
	/* Set defaults */
	if (ac->config.session_timeout == 0)
		ac->config.session_timeout = DEFAULT_SESSION_TIMEOUT;
	if (ac->config.max_sessions == 0)
		ac->config.max_sessions = MAX_SESSIONS;
	if (ac->config.max_api_keys == 0)
		ac->config.max_api_keys = MAX_API_KEYS;
	if (ac->config.password_min_length == 0)
		ac->config.password_min_length = DEFAULT_PASSWORD_MIN_LENGTH;
	if (ac->config.max_login_attempts == 0)
		ac->config.max_login_attempts = 5;
	if (ac->config.lockout_duration == 0)
		ac->config.lockout_duration = 300;
	
	/* Initialize mutexes */
	if (pthread_mutex_init(&ac->users_mutex, NULL) != 0 ||
	    pthread_mutex_init(&ac->sessions_mutex, NULL) != 0 ||
	    pthread_mutex_init(&ac->api_keys_mutex, NULL) != 0 ||
	    pthread_mutex_init(&ac->login_attempts_mutex, NULL) != 0) {
		pthread_mutex_destroy(&ac->users_mutex);
		pthread_mutex_destroy(&ac->sessions_mutex);
		pthread_mutex_destroy(&ac->api_keys_mutex);
		pthread_mutex_destroy(&ac->login_attempts_mutex);
		free(ac);
		return NULL;
	}
	
	atomic_init(&ac->failed_logins, 0);
	atomic_init(&ac->successful_logins, 0);
	
	return ac;
}

void access_control_destroy(struct access_control *ac)
{
	if (!ac)
		return;
	
	pthread_mutex_destroy(&ac->users_mutex);
	pthread_mutex_destroy(&ac->sessions_mutex);
	pthread_mutex_destroy(&ac->api_keys_mutex);
	pthread_mutex_destroy(&ac->login_attempts_mutex);
	
	free(ac);
}

int access_control_add_user(struct access_control *ac,
                            const char *username,
                            const char *password,
                            enum user_role role)
{
	struct ac_user *user;
	
	if (!ac || !username || !password)
		return -1;
	
	/* Validate password strength */
	if (ac->config.require_strong_passwords) {
		if (!validate_password_strength(password, ac->config.password_min_length))
			return -1;
	}
	
	pthread_mutex_lock(&ac->users_mutex);
	
	/* Check if user exists */
	for (size_t i = 0; i < ac->num_users; i++) {
		if (strcmp(ac->users[i].username, username) == 0) {
			pthread_mutex_unlock(&ac->users_mutex);
			return -1;
		}
	}
	
	if (ac->num_users >= MAX_USERS) {
		pthread_mutex_unlock(&ac->users_mutex);
		return -1;
	}
	
	user = &ac->users[ac->num_users];
	memset(user, 0, sizeof(*user));
	
	strncpy(user->username, username, sizeof(user->username) - 1);
	hash_password(password, user->password_hash, sizeof(user->password_hash));
	user->role = role;
	user->permissions = access_control_get_role_permissions(role);
	user->active = true;
	user->created = time(NULL);
	
	ac->num_users++;
	
	pthread_mutex_unlock(&ac->users_mutex);
	
	return 0;
}

int access_control_remove_user(struct access_control *ac, const char *username)
{
	if (!ac || !username)
		return -1;
	
	pthread_mutex_lock(&ac->users_mutex);
	
	for (size_t i = 0; i < ac->num_users; i++) {
		if (strcmp(ac->users[i].username, username) == 0) {
			ac->users[i].active = false;
			pthread_mutex_unlock(&ac->users_mutex);
			return 0;
		}
	}
	
	pthread_mutex_unlock(&ac->users_mutex);
	return -1;
}

int access_control_login(struct access_control *ac,
                        const char *username,
                        const char *password,
                        const char *ip_address,
                        char *session_id,
                        size_t session_id_len)
{
	struct ac_user *user = NULL;
	struct ac_session *session;
	char password_hash[128];
	time_t now = time(NULL);
	
	if (!ac || !username || !password || !session_id)
		return -1;
	
	/* Check for account lockout */
	pthread_mutex_lock(&ac->login_attempts_mutex);
	for (size_t i = 0; i < ac->num_login_attempts; i++) {
		if (strcmp(ac->login_attempts[i].username, username) == 0) {
			if (now < ac->login_attempts[i].lockout_until) {
				pthread_mutex_unlock(&ac->login_attempts_mutex);
				return -1;
			}
			break;
		}
	}
	pthread_mutex_unlock(&ac->login_attempts_mutex);
	
	/* Find and validate user */
	pthread_mutex_lock(&ac->users_mutex);
	for (size_t i = 0; i < ac->num_users; i++) {
		if (strcmp(ac->users[i].username, username) == 0 && ac->users[i].active) {
			user = &ac->users[i];
			break;
		}
	}
	
	if (!user) {
		pthread_mutex_unlock(&ac->users_mutex);
		atomic_fetch_add_explicit(&ac->failed_logins, 1, memory_order_relaxed);
		return -1;
	}
	
	hash_password(password, password_hash, sizeof(password_hash));
	
	if (strcmp(password_hash, user->password_hash) != 0) {
		pthread_mutex_unlock(&ac->users_mutex);
		
		/* Track failed login attempt */
		pthread_mutex_lock(&ac->login_attempts_mutex);
		struct login_attempt *attempt = NULL;
		for (size_t i = 0; i < ac->num_login_attempts; i++) {
			if (strcmp(ac->login_attempts[i].username, username) == 0) {
				attempt = &ac->login_attempts[i];
				break;
			}
		}
		
		if (!attempt && ac->num_login_attempts < MAX_USERS) {
			attempt = &ac->login_attempts[ac->num_login_attempts++];
			strncpy(attempt->username, username, sizeof(attempt->username) - 1);
			attempt->attempts = 0;
		}
		
		if (attempt) {
			attempt->attempts++;
			if (attempt->attempts >= ac->config.max_login_attempts) {
				attempt->lockout_until = now + ac->config.lockout_duration;
			}
		}
		pthread_mutex_unlock(&ac->login_attempts_mutex);
		
		atomic_fetch_add_explicit(&ac->failed_logins, 1, memory_order_relaxed);
		return -1;
	}
	
	/* Update user stats */
	user->last_login = now;
	user->login_count++;
	
	pthread_mutex_unlock(&ac->users_mutex);
	
	/* Create session */
	pthread_mutex_lock(&ac->sessions_mutex);
	
	if (ac->num_sessions >= ac->config.max_sessions) {
		/* Remove oldest inactive session */
		for (size_t i = 0; i < ac->num_sessions; i++) {
			if (!ac->sessions[i].active || now > ac->sessions[i].expires) {
				memmove(&ac->sessions[i], &ac->sessions[i + 1],
				        sizeof(struct ac_session) * (ac->num_sessions - i - 1));
				ac->num_sessions--;
				break;
			}
		}
	}
	
	if (ac->num_sessions >= ac->config.max_sessions) {
		pthread_mutex_unlock(&ac->sessions_mutex);
		return -1;
	}
	
	session = &ac->sessions[ac->num_sessions];
	memset(session, 0, sizeof(*session));
	
	generate_random_string(session->session_id, sizeof(session->session_id));
	strncpy(session->username, username, sizeof(session->username) - 1);
	session->role = user->role;
	session->permissions = user->permissions;
	session->created = now;
	session->expires = now + ac->config.session_timeout;
	session->last_activity = now;
	if (ip_address)
		strncpy(session->ip_address, ip_address, sizeof(session->ip_address) - 1);
	session->active = true;
	
	strncpy(session_id, session->session_id, session_id_len - 1);
	
	ac->num_sessions++;
	
	pthread_mutex_unlock(&ac->sessions_mutex);
	
	/* Clear failed login attempts */
	pthread_mutex_lock(&ac->login_attempts_mutex);
	for (size_t i = 0; i < ac->num_login_attempts; i++) {
		if (strcmp(ac->login_attempts[i].username, username) == 0) {
			ac->login_attempts[i].attempts = 0;
			ac->login_attempts[i].lockout_until = 0;
			break;
		}
	}
	pthread_mutex_unlock(&ac->login_attempts_mutex);
	
	atomic_fetch_add_explicit(&ac->successful_logins, 1, memory_order_relaxed);
	
	return 0;
}

int access_control_validate_session(struct access_control *ac,
                                    const char *session_id,
                                    char *username,
                                    size_t username_len)
{
	time_t now = time(NULL);
	
	if (!ac || !session_id)
		return -1;
	
	pthread_mutex_lock(&ac->sessions_mutex);
	
	for (size_t i = 0; i < ac->num_sessions; i++) {
		struct ac_session *session = &ac->sessions[i];
		
		if (session->active && strcmp(session->session_id, session_id) == 0) {
			if (now > session->expires) {
				session->active = false;
				pthread_mutex_unlock(&ac->sessions_mutex);
				return -1;
			}
			
			/* Renew session if enabled */
			if (ac->config.enable_session_renewal) {
				session->last_activity = now;
				session->expires = now + ac->config.session_timeout;
			}
			
			if (username)
				strncpy(username, session->username, username_len - 1);
			
			pthread_mutex_unlock(&ac->sessions_mutex);
			return 0;
		}
	}
	
	pthread_mutex_unlock(&ac->sessions_mutex);
	return -1;
}

bool access_control_check_permission(struct access_control *ac,
                                     const char *session_id,
                                     enum permission permission)
{
	time_t now = time(NULL);
	bool authorized = false;
	
	if (!ac || !session_id)
		return false;
	
	pthread_mutex_lock(&ac->sessions_mutex);
	
	for (size_t i = 0; i < ac->num_sessions; i++) {
		struct ac_session *session = &ac->sessions[i];
		
		if (session->active && strcmp(session->session_id, session_id) == 0) {
			if (now <= session->expires) {
				authorized = (session->permissions & permission) != 0;
			}
			break;
		}
	}
	
	pthread_mutex_unlock(&ac->sessions_mutex);
	
	return authorized;
}

int access_control_create_api_key(struct access_control *ac,
                                  const char *name,
                                  const char *owner,
                                  enum user_role role,
                                  uint32_t expires_in,
                                  char *key,
                                  size_t key_len)
{
	struct ac_api_key *api_key;
	char raw_key[128];
	time_t now = time(NULL);
	
	if (!ac || !name || !owner || !key)
		return -1;
	
	pthread_mutex_lock(&ac->api_keys_mutex);
	
	if (ac->num_api_keys >= ac->config.max_api_keys) {
		pthread_mutex_unlock(&ac->api_keys_mutex);
		return -1;
	}
	
	api_key = &ac->api_keys[ac->num_api_keys];
	memset(api_key, 0, sizeof(*api_key));
	
	/* Generate key ID and raw key */
	generate_random_string(api_key->key_id, sizeof(api_key->key_id));
	generate_random_string(raw_key, sizeof(raw_key));
	
	/* Hash the key for storage */
	hash_password(raw_key, api_key->key_hash, sizeof(api_key->key_hash));
	
	strncpy(api_key->name, name, sizeof(api_key->name) - 1);
	strncpy(api_key->owner, owner, sizeof(api_key->owner) - 1);
	api_key->role = role;
	api_key->permissions = access_control_get_role_permissions(role);
	api_key->created = now;
	api_key->expires = expires_in > 0 ? now + expires_in : 0;
	api_key->active = true;
	
	/* Return the raw key (only time it's available) */
	snprintf(key, key_len, "%s.%s", api_key->key_id, raw_key);
	
	ac->num_api_keys++;
	
	pthread_mutex_unlock(&ac->api_keys_mutex);
	
	return 0;
}

int access_control_validate_api_key(struct access_control *ac,
                                    const char *key,
                                    char *owner,
                                    size_t owner_len)
{
	char key_copy[256];
	char *key_id, *raw_key;
	char key_hash[128];
	time_t now = time(NULL);
	
	if (!ac || !key)
		return -1;
	
	strncpy(key_copy, key, sizeof(key_copy) - 1);
	key_copy[sizeof(key_copy) - 1] = '\0';
	
	key_id = strtok(key_copy, ".");
	raw_key = strtok(NULL, ".");
	
	if (!key_id || !raw_key)
		return -1;
	
	hash_password(raw_key, key_hash, sizeof(key_hash));
	
	pthread_mutex_lock(&ac->api_keys_mutex);
	
	for (size_t i = 0; i < ac->num_api_keys; i++) {
		struct ac_api_key *api_key = &ac->api_keys[i];
		
		if (api_key->active && strcmp(api_key->key_id, key_id) == 0) {
			if (strcmp(api_key->key_hash, key_hash) != 0) {
				pthread_mutex_unlock(&ac->api_keys_mutex);
				return -1;
			}
			
			if (api_key->expires > 0 && now > api_key->expires) {
				api_key->active = false;
				pthread_mutex_unlock(&ac->api_keys_mutex);
				return -1;
			}
			
			api_key->usage_count++;
			api_key->last_used = now;
			
			if (owner)
				strncpy(owner, api_key->owner, owner_len - 1);
			
			pthread_mutex_unlock(&ac->api_keys_mutex);
			return 0;
		}
	}
	
	pthread_mutex_unlock(&ac->api_keys_mutex);
	return -1;
}

const char *access_control_role_string(enum user_role role)
{
	switch (role) {
	case ROLE_VIEWER:
		return "viewer";
	case ROLE_OPERATOR:
		return "operator";
	case ROLE_ADMIN:
		return "admin";
	default:
		return "unknown";
	}
}

int access_control_cleanup_expired(struct access_control *ac)
{
	time_t now = time(NULL);
	int cleaned = 0;
	
	if (!ac)
		return 0;
	
	/* Clean expired sessions */
	pthread_mutex_lock(&ac->sessions_mutex);
	for (size_t i = 0; i < ac->num_sessions; ) {
		if (!ac->sessions[i].active || now > ac->sessions[i].expires) {
			memmove(&ac->sessions[i], &ac->sessions[i + 1],
			        sizeof(struct ac_session) * (ac->num_sessions - i - 1));
			ac->num_sessions--;
			cleaned++;
		} else {
			i++;
		}
	}
	pthread_mutex_unlock(&ac->sessions_mutex);
	
	/* Clean expired API keys */
	pthread_mutex_lock(&ac->api_keys_mutex);
	for (size_t i = 0; i < ac->num_api_keys; i++) {
		if (ac->api_keys[i].expires > 0 && now > ac->api_keys[i].expires) {
			ac->api_keys[i].active = false;
			cleaned++;
		}
	}
	pthread_mutex_unlock(&ac->api_keys_mutex);
	
	return cleaned;
}

void access_control_stats(struct access_control *ac,
                         size_t *total_users,
                         size_t *active_sessions,
                         size_t *total_api_keys,
                         unsigned long *failed_logins)
{
	if (!ac)
		return;
	
	if (total_users) {
		pthread_mutex_lock(&ac->users_mutex);
		*total_users = ac->num_users;
		pthread_mutex_unlock(&ac->users_mutex);
	}
	
	if (active_sessions) {
		pthread_mutex_lock(&ac->sessions_mutex);
		*active_sessions = ac->num_sessions;
		pthread_mutex_unlock(&ac->sessions_mutex);
	}
	
	if (total_api_keys) {
		pthread_mutex_lock(&ac->api_keys_mutex);
		*total_api_keys = ac->num_api_keys;
		pthread_mutex_unlock(&ac->api_keys_mutex);
	}
	
	if (failed_logins)
		*failed_logins = atomic_load_explicit(&ac->failed_logins, memory_order_relaxed);
}
