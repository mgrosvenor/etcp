/*
 * Copyright (c) 2016, All rights reserved.
 * See LICENSE.txt for full details. 
 * 
 *  Created:   30 Mar 2016
 *  File name: utils.h
 *  Description:
 *  <INSERT DESCRIPTION HERE> 
 */
#ifndef SRC_UTILS_H_
#define SRC_UTILS_H_

#define if_likely(x)       if(__builtin_expect((x),1))
#define if_unlikely(x)     if(__builtin_expect((x),0))
#define if_eqlikely(x)     if(x)
#define MIN(x,y) ( (x) < (y) ?  (x) : (y))


#endif /* SRC_UTILS_H_ */
