/* 
 * File:   smbfs.h
 * Author: cc
 *
 * Created on 4 août 2012, 17:51
 */

#ifndef SMBFS_H
#define	SMBFS_H

#ifdef	__cplusplus
extern "C" {
#endif

/**
 * Return 1 if success
 * @param mount_name
 * @param ip
 * @param port
 * @param share
 * @param user
 * @param password
 * @return 
 */
int smbInit(const char * mount_name, const char * ip, int port, const char * share, const char * user, const char * password);


#ifdef	__cplusplus
}
#endif

#endif	/* SMBFS_H */

