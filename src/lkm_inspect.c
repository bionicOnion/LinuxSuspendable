#include "types.h"
#include "lkm_utils.h"
#include "lkm_export.h"

#include <linux/kobject.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/sysfs.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/pid.h>
#include <linux/slab.h>

#define AVAILABLE 0
#define BUSY 1

static int lkm_state = AVAILABLE;
static LKM_Operation_t lkm_opstruct;

static void __generate_path( char *dest, char *dir, char *filename )
{

  memset( dest, 0, MAX_PATH_SIZE );

  if( strlen(dir) == 0 )
  {
    snprintf( dest, MAX_PATH_SIZE, "%s", filename );
  }
  else
  {
    snprintf( dest, MAX_PATH_SIZE, "%s/%s", dir, filename );
  }
}

static ssize_t operation_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf)
{
  // TODO -- Print out usage message that this doesn't actually exist
  printk( KERN_WARNING "linux_inspect->read operation not currently supported\n" );
  return 0;
}

static ssize_t operation_store(struct kobject *kobj, struct kobj_attribute *attr, const char *buf, size_t count)
{

  // TODO - Do we need to copy the buffer into a queue and have a separate kernel thread process the elements?

  int rv = 0;
  struct task_struct *task_ptr = 0;
  char file_name[MAX_FILENAME_SIZE];
  char full_path[MAX_PATH_SIZE];

  // copies the operation struct from userspace
  memcpy( &lkm_opstruct, buf, sizeof(LKM_Operation_t));
  // copy_from_user( &lkm_opstruct, buf, sizeof(LKM_Operation_t));
  memset( file_name, 0, MAX_FILENAME_SIZE );
  memset( full_path, 0, MAX_PATH_SIZE );

  printk(KERN_DEBUG "-------------------------------------------------\n");
  printk(KERN_DEBUG "linux_inspect->cmd=0x%08x\n", lkm_opstruct.cmd );
  printk(KERN_DEBUG "linux_inspect->pid=%d\n", lkm_opstruct.proc_id );
  printk(KERN_DEBUG "linux_inspect->directory=%s\n", lkm_opstruct.dir_name );

  if( lkm_state == BUSY )
    return 0;

  // acquire read lock for the task_struct area
  rcu_read_lock();

  // retrieve pointer to task_struct
  task_ptr = lkm_get_task_struct( lkm_opstruct.proc_id );
  if( task_ptr == NULL )
  {
    printk( KERN_WARNING "linux_inspect->invalid pid; %d\n", lkm_opstruct.proc_id );
    return count;
  }

  // prevent the task from being scheduled while dumping
  set_task_state(task_ptr, TASK_UNINTERRUPTIBLE);

  // start dumping corresponding sections of the process
  if( (rv == 0) && (lkm_opstruct.cmd & LKM_TASK_STRUCT) )
  {

    LKM_FILE file;
    unsigned long long offset = 0;

    // creates the full path
    __generate_path( full_path, lkm_opstruct.dir_name, "task_struct.txt" );

    file = lkm_file_open( full_path, LKM_Write );
    if( file == NULL )
    {
      printk( KERN_WARNING "linux_inspect->Failed to open %s\n", full_path );
      return count;
    }

    if( lkm_export_task_struct( task_ptr, file, &offset ) < 0 )
      printk( KERN_WARNING "linux_inspect->Failed to export task_struct\n" );

    // close file
    lkm_file_close( file );

  }
  if( (rv == 0) && (lkm_opstruct.cmd & LKM_TASK_MEMORY) )
  {

    LKM_FILE file;
    unsigned long long offset = 0;

    // creates the full path
    __generate_path( full_path, lkm_opstruct.dir_name, "task_memory_struct.txt" );

    file = lkm_file_open( full_path, LKM_Write );
    if( file == NULL )
    {
      printk( KERN_WARNING "linux_inspect->Failed to open %s\n", full_path );
      return count;
    }

    if( lkm_export_task_memory( task_ptr, file, &offset ) < 0 )
      printk( KERN_WARNING "linux_inspect->Failed to export task_struct\n" );

    // close file
    lkm_file_close( file );

  }

  // reset the process state to allow it to be scheduled again
  set_task_state(task_ptr, TASK_RUNNING);

  // release lock over the task_struct listing
  rcu_read_unlock();

  // TODO -- Check if this is necessary
  if( rv != 0 )
    return count;

  return count;
}

static struct kobject *pid_kobj;

static struct kobj_attribute operation_attribute =
  __ATTR(operation, 0664, operation_show, operation_store );
  
static struct attribute *attrs[] = {
  &operation_attribute.attr,
  NULL, /* need to NULL terminate the list of attributes */
};

static struct attribute_group attr_group = {
  .attrs = attrs,
};

// module installation point
static int lkm_init(void)
{
  int retval = 0;
	
  printk( KERN_DEBUG "linux_inspect->init\n" );

  // clear operation
  memset( &lkm_opstruct, 0, sizeof( LKM_Operation_t ) );
	
  // creates directory for interacting with module
  pid_kobj = kobject_create_and_add("linux_inspect", kernel_kobj);
  if (!pid_kobj)
    return -ENOMEM;

  /* Create the files associated with this kobject */
  retval = sysfs_create_group(pid_kobj, &attr_group);
  if (retval)
    kobject_put(pid_kobj);
	
  return 0;
}

// module removal point
static void lkm_exit(void)
{
  printk(KERN_DEBUG "linux_inspect->removed\n");
  kobject_put(pid_kobj);

  // kfree(lkm_task_struct_contents);
}

// Sets up callback functions
module_init(lkm_init);
module_exit(lkm_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Cameron Whipple & Robert Miller");
MODULE_DESCRIPTION("Linux module to allow for inspecting the task_struct for user space process.");
