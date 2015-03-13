/*
 *   This program is is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or (at
 *   your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

/**
 * $Id$
 * @file auth_wbclient.c
 * @brief NTLM authentication against the wbclient library
 *
 * @copyright 2015  Matthew Newton
 */

RCSID("$Id$")

#include <freeradius-devel/radiusd.h>
#include <freeradius-devel/rad_assert.h>

#include <wbclient.h>

#include "rlm_mschap.h"
#include "mschap.h"
#include "auth_wbclient.h"

#define NT_LENGTH 24

/*
 *	Check NTLM authentication direct to winbind via
 *	Samba's libwbclient library
 *
 *	Returns -1 for failure and 0 on auth success
 */
int do_auth_wbclient(rlm_mschap_t *inst, REQUEST *request,
		     uint8_t const *challenge, uint8_t const *response,
		     uint8_t nthashhash[NT_DIGEST_LENGTH])
{
	int rcode = -1;
	struct wbcAuthUserParams authparams;
	wbcErr err;
	int len;
	struct wbcAuthUserInfo *info = NULL;
	struct wbcAuthErrorInfo *error = NULL;
	char user_name_buf[500];
	char domain_name_buf[500];
	uint8_t resp[NT_LENGTH];

	/*
	 * Clear the auth parameters - this is important, as
	 * there are options that will cause wbcAuthenticateUserEx
	 * to bomb out if not zero.
	 */
	memset(&authparams, 0, sizeof(authparams));

	/*
	 * wb_username must be set for this function to be called
	 */
	rad_assert(inst->wb_username);

	/*
	 * Get the username and domain from the configuration
	 */
	len = tmpl_expand(&authparams.account_name, user_name_buf, sizeof(user_name_buf),
			  request, inst->wb_username, NULL, NULL);
	if (len < 0) {
		REDEBUG2("Unable to expand winbind_username");
		goto done;
	}

	if (inst->wb_domain) {
		len = tmpl_expand(&authparams.domain_name, domain_name_buf, sizeof(domain_name_buf),
				  request, inst->wb_domain, NULL, NULL);
		if (len < 0) {
			REDEBUG2("Unable to expand winbind_domain");
			goto done;
		}
	} else {
		RWDEBUG2("No domain specified; authentication may fail because of this");
	}


	/*
	 * Build the wbcAuthUserParams structure with what we know
	 */
	authparams.level = WBC_AUTH_USER_LEVEL_RESPONSE;
	authparams.password.response.nt_length = NT_LENGTH;

	memcpy(resp, response, NT_LENGTH);
	authparams.password.response.nt_data = resp;

	memcpy(authparams.password.response.challenge, challenge,
	       sizeof(authparams.password.response.challenge));


	/*
	 * Send auth request across to winbind
	 */
	RDEBUG2("sending authentication request user='%s' domain='%s'", authparams.account_name,
									authparams.domain_name);

	err = wbcCtxAuthenticateUserEx(inst->wb_ctx, &authparams, &info, &error);


	/*
	 * Try and give some useful feedback on what happened
	 */
	switch (err) {
	case WBC_ERR_SUCCESS:
		rcode = 0;
		RDEBUG2("Authenticated successfully");
		/* Grab the nthashhash from the result */
		memcpy(nthashhash, info->user_session_key, NT_DIGEST_LENGTH);
		break;
	case WBC_ERR_WINBIND_NOT_AVAILABLE:
		RERROR("Unable to contact winbind!");
		RERROR("Check that winbind is running and that FreeRADIUS");
		RERROR("has permission to connect to the winbind socket.");
		break;
	case WBC_ERR_DOMAIN_NOT_FOUND:
		REDEBUG2("Authentication failed: domain not found");
		break;
	case WBC_ERR_AUTH_ERROR:
		REDEBUG2("Authentication failed (check domain is correct)");
		break;
	default:
		REDEBUG2("Authentication failed: wbcErr %d", err);
		if (error && error->display_string) {
			REDEBUG2("wbcErr %s", error->display_string);
		}
		break;
	}


done:
	if (info) wbcFreeMemory(info);
	if (error) wbcFreeMemory(error);

	return rcode;
}
