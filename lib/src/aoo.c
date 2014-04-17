/* Copyright (c) 2014 Winfried Ritsch
 * 
 * This library is covered by the LGPL, read licences
 * at <http://www.gnu.org/licenses/>  for details 
 * 
 */

/* initialize lib 
 * 
 * required: 
 */

int aoo_setup(void)
{
return -1;
}

/* release lib  * 
 * required: 
 */

/* int aoo_release(void) {}         */

/* --- drains  --- */

/* setup new drain  
 * 
 * required:  aoo_setup
 */
int aoo_drain_new(int id)
{
	return 0;
}

/* start processing 
 *
 * required: aoo_new
 */
int aoo_drain_start(int id)
{
	return 0;
}


/*  process 
 *
 * required: aoo_start
 */
int aoo_drain_process(int id)
{
	return 0;
}

/* stop processing
 *
 * required: aoo_start
 */
int aoo_drain_stop(int id)
{
	return 0;
}
  
/* free drain  
 * 
 * required: aoo_new
 */
int aoo_drain_free(int id)
{
	return 0;
}
