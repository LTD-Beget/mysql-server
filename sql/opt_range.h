/* Copyright (C) 2000 MySQL AB & MySQL Finland AB & TCX DataKonsult AB

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */


/* classes to use when handling where clause */

#ifndef _opt_range_h
#define _opt_range_h

#ifdef __GNUC__
#pragma interface			/* gcc class implementation */
#endif

#define NO_MIN_RANGE	1
#define NO_MAX_RANGE	2
#define NEAR_MIN	4
#define NEAR_MAX	8
#define UNIQUE_RANGE	16
#define EQ_RANGE	32
#define NULL_RANGE	64
#define GEOM_FLAG      128


typedef struct st_key_part {
  uint16           key,part,part_length;
  uint8            null_bit;
  Field            *field;
  Field::imagetype image_type;
} KEY_PART;


class QUICK_RANGE :public Sql_alloc {
 public:
  char *min_key,*max_key;
  uint16 min_length,max_length,flag;
#ifdef HAVE_purify
  uint16 dummy;					/* Avoid warnings on 'flag' */
#endif
  QUICK_RANGE();				/* Full range */
  QUICK_RANGE(const char *min_key_arg,uint min_length_arg,
	      const char *max_key_arg,uint max_length_arg,
	      uint flag_arg)
    : min_key((char*) sql_memdup(min_key_arg,min_length_arg+1)),
      max_key((char*) sql_memdup(max_key_arg,max_length_arg+1)),
      min_length((uint16) min_length_arg),
      max_length((uint16) max_length_arg),
      flag((uint16) flag_arg)
    {
#ifdef HAVE_purify
      dummy=0;
#endif
    }
};

class INDEX_MERGE; 

/*
  Quick select interface. 
  This class is parent for all QUICK_*_SELECT and FT_SELECT classes.
*/

class QUICK_SELECT_I
{
public:
  ha_rows records;  /* estimate of # of records to be retrieved */
  double  read_time; /* time to perform this retrieval          */
  TABLE   *head;

  /*
    the only index this quick select uses, or MAX_KEY for 
    QUICK_INDEX_MERGE_SELECT
  */
  uint index; 
  uint max_used_key_length, used_key_parts;

  QUICK_SELECT_I();
  virtual ~QUICK_SELECT_I(){};
  virtual int  init() = 0;
  virtual void reset(void) = 0;
  virtual int  get_next() = 0;   /* get next record to retrieve */
  virtual bool reverse_sorted() = 0;
  virtual bool unique_key_range() { return false; }

  enum { 
    QS_TYPE_RANGE = 0,
    QS_TYPE_INDEX_MERGE = 1,
    QS_TYPE_RANGE_DESC = 2,
    QS_TYPE_FULLTEXT   = 3
  };

  /* Get type of this quick select - one of the QS_* values */
  virtual int get_type() = 0; 
};

struct st_qsel_param;
class SEL_ARG;

class QUICK_RANGE_SELECT : public QUICK_SELECT_I 
{
protected:
  bool next,dont_free;
public:
  int error;
  handler *file;
  byte    *record;
protected:
  friend void print_quick_sel_range(QUICK_RANGE_SELECT *quick,
                                    const key_map* needed_reg);
  friend QUICK_RANGE_SELECT *get_quick_select_for_ref(THD *thd, TABLE *table, 
                                                      struct st_table_ref *ref);
  friend bool get_quick_keys(struct st_qsel_param *param,
                             QUICK_RANGE_SELECT *quick,KEY_PART *key,
                             SEL_ARG *key_tree,char *min_key,uint min_key_flag,
                             char *max_key, uint max_key_flag);
  friend QUICK_RANGE_SELECT *get_quick_select(struct st_qsel_param*,uint idx,
                                              SEL_ARG *key_tree,
                                              MEM_ROOT *alloc);
  friend class QUICK_SELECT_DESC;

  List<QUICK_RANGE> ranges;
  List_iterator<QUICK_RANGE> it;
  QUICK_RANGE *range;
  MEM_ROOT alloc;
  KEY_PART *key_parts;  
  int cmp_next(QUICK_RANGE *range);
public:
  QUICK_RANGE_SELECT(THD *thd, TABLE *table,uint index_arg,bool no_alloc=0,
                     MEM_ROOT *parent_alloc=NULL);
  ~QUICK_RANGE_SELECT();
  
  void reset(void) { next=0; it.rewind(); }
  int init();
  int get_next();
  bool reverse_sorted() { return 0; }
  bool unique_key_range();
  int get_type() { return QS_TYPE_RANGE; }
};

/*
  Helper class for keeping track of rows that have been passed to output 
  in index_merge access method. 

  NOTES
    Current implementation uses a temporary table to store ROWIDs of rows that
    have been passed to output. In the future it might be changed to use more 
    efficient mechanisms, like Unique class.
*/

class INDEX_MERGE
{
public:
  INDEX_MERGE(THD *thd_arg);
  ~INDEX_MERGE(); 
  
  int init(TABLE *table);
  int check_record_in();
  int start_last_quick_select();
  int error;
private:
  /* The only field in temporary table */
  class Item_rowid : public Item_str_func
  {
    TABLE *head; /* source table */
  public:
    Item_rowid(TABLE *table) : head(table) 
    {
      max_length= table->file->ref_length;
      collation.set(&my_charset_bin);
    };
    const char *func_name() const { return "rowid"; }
    bool const_item() const { return 0; }
    String *val_str(String *);
    void fix_length_and_dec()
    {}
  };

  /* Check if record has been processed and save it if it wasn't  */
  inline int put_record(); 
  
  /* Check if record has been processed without saving it         */
  inline int check_record();
  
  /* If true, check_record_in does't store ROWIDs it is passed.   */
  bool  dont_save;

  THD *thd;
  TABLE *head;                     /* source table                        */
  TABLE *temp_table;               /* temp. table used for values storage */
  TMP_TABLE_PARAM tmp_table_param; /* temp. table creation parameters     */
  Item_rowid *rowid_item;          /* the only field in temp. table       */
  List<Item> fields;               /* temp. table fields list 
                                      (the only element is rowid_item)    */
  ORDER order;                     /* key for temp. table (rowid_item)    */
};


/*
  Index merge quick select. 
  It is implemented as a container for several QUICK_RANGE_SELECTs.
*/

class QUICK_INDEX_MERGE_SELECT : public QUICK_SELECT_I 
{
public:
  QUICK_INDEX_MERGE_SELECT(THD *thd, TABLE *table);
  ~QUICK_INDEX_MERGE_SELECT();

  int  init();
  void reset(void);
  int  get_next();
  bool reverse_sorted() { return false; }
  bool unique_key_range() { return false; }
  int get_type() { return QS_TYPE_INDEX_MERGE; }

  bool push_quick_back(QUICK_RANGE_SELECT *quick_sel_range);

  /* range quick selects this index_merge read consists of */
  List<QUICK_RANGE_SELECT> quick_selects;
  
  /* quick select which is currently used for rows retrieval */
  List_iterator_fast<QUICK_RANGE_SELECT> cur_quick_it;
  QUICK_RANGE_SELECT* cur_quick_select;
  
  /*
    Last element in quick_selects list. 
    INDEX_MERGE::start_last_quick_select is called before retrieving
    rows for it. 
  */
  QUICK_RANGE_SELECT* last_quick_select;
  
  /*
    Used to keep track of what records have been already passed to output 
    when doing index_merge access (NULL means no index_merge) 
  */
  INDEX_MERGE index_merge;

  MEM_ROOT    alloc;
};

class QUICK_SELECT_DESC: public QUICK_RANGE_SELECT
{
public:
  QUICK_SELECT_DESC(QUICK_RANGE_SELECT *q, uint used_key_parts);
  int get_next();
  bool reverse_sorted() { return 1; }
  int get_type() { return QS_TYPE_RANGE_DESC; }
private:
  int cmp_prev(QUICK_RANGE *range);
  bool range_reads_after_key(QUICK_RANGE *range);
#ifdef NOT_USED
  bool test_if_null_range(QUICK_RANGE *range, uint used_key_parts);
#endif
  void reset(void) { next=0; rev_it.rewind(); }
  List<QUICK_RANGE> rev_ranges;
  List_iterator<QUICK_RANGE> rev_it;
};


class SQL_SELECT :public Sql_alloc {
 public:
  QUICK_SELECT_I *quick;	// If quick-select used
  COND		*cond;		// where condition
  TABLE	*head;
  IO_CACHE file;		// Positions to used records
  ha_rows records;		// Records in use if read from file
  double read_time;		// Time to read rows
  key_map quick_keys;		// Possible quick keys
  key_map needed_reg;		// Possible quick keys after prev tables.
  table_map const_tables,read_tables;
  bool	free_cond;

  SQL_SELECT();
  ~SQL_SELECT();
  bool check_quick(THD *thd, bool force_quick_range, ha_rows limit)
  { return test_quick_select(thd, key_map(~0), 0, limit, force_quick_range) < 0; }
  inline bool skipp_record() { return cond ? cond->val_int() == 0 : 0; }
  int test_quick_select(THD *thd, key_map keys, table_map prev_tables,
			ha_rows limit, bool force_quick_range=0);
};

QUICK_RANGE_SELECT *get_quick_select_for_ref(THD *thd, TABLE *table,
				       struct st_table_ref *ref);

#endif
