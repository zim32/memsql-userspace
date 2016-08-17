#include <iostream>
#include <cstdio>
#include <typeinfo>
#include <QtSql>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
extern "C" {
	#include "types.h"
}



static bool is_structure_empty(void *buf, size_t size){
	for(size_t i=0; i<size; i++){
		if(*(static_cast<char *>(buf)+i) != 0){
			return false;
		}
	}
	return true;
}


static void process_data(zim32_task_t &task, QSqlDatabase &db){
	QSqlQuery query;
	query.prepare("INSERT INTO task(cmd, raw_id) VALUES(:cmd, :raw_id)");
	query.bindValue(":cmd", task.cmd);
	query.bindValue(":raw_id", reinterpret_cast<unsigned long long>(task.id));
	if(!query.exec()){
		QSqlError err = query.lastError();
		std::cerr << "Query error" << std::endl;
		std::cerr << err.text().toStdString() << std::endl;
	}
}

static void process_data(zim32_mm_t &mm, QSqlDatabase &db){
	QSqlQuery query;


	// find task id
	query.prepare("SELECT id FROM task WHERE raw_id = :raw_id LIMIT 1");
	query.bindValue(":raw_id", reinterpret_cast<unsigned long long>(mm.task_id));
	if(!query.exec()){
		QSqlError err = query.lastError();
		std::cerr << "Query error: ";
		std::cerr << err.text().toStdString() << std::endl;
		return;
	}
	query.first();
	QVariant task_id = query.value(0);



	query.prepare(""
			"INSERT INTO mm(raw_id, start_code, end_code, start_data, end_data, start_brk, brk, start_stack, task_raw_id, task_id) "
			"VALUES(:raw_id, :start_code, :end_code, :start_data, :end_data, :start_brk, :brk, :start_stack, :task_raw_id, :task_id)");
	query.bindValue(":raw_id", reinterpret_cast<unsigned long long>(mm.id));
	query.bindValue(":start_code", static_cast<unsigned long long>(mm.start_code));
	query.bindValue(":end_code", static_cast<unsigned long long>(mm.end_code));
	query.bindValue(":start_data", static_cast<unsigned long long>(mm.start_data));
	query.bindValue(":end_data", static_cast<unsigned long long>(mm.end_data));
	query.bindValue(":start_brk", static_cast<unsigned long long>(mm.start_brk));
	query.bindValue(":brk", static_cast<unsigned long long>(mm.brk));
	query.bindValue(":start_stack", static_cast<unsigned long long>(mm.start_stack));
	query.bindValue(":task_raw_id", reinterpret_cast<unsigned long long>(mm.task_id));
	query.bindValue(":task_id", task_id);
	if(!query.exec()){
		QSqlError err = query.lastError();
		std::cerr << "Query error: ";
		std::cerr << err.text().toStdString() << std::endl;
	}

}

static void process_data(zim32_vm_area_t &vmarea, QSqlDatabase &db){
	QSqlQuery query;

	query.prepare(""
			"INSERT INTO vm_area(raw_id, start_address, end_address, backed_file_name, backed_file_offset, mm_raw_id) "
			"VALUES(:raw_id, :start_address, :end_address, :backed_file_name, :backed_file_offset, :mm_raw_id)");
	query.bindValue(":raw_id", reinterpret_cast<unsigned long long>(vmarea.id));
	query.bindValue(":start_address", static_cast<unsigned long long>(vmarea.vm_start));
	query.bindValue(":end_address", static_cast<unsigned long long>(vmarea.vm_end));
	query.bindValue(":backed_file_name", vmarea.file_name);
	query.bindValue(":backed_file_offset", static_cast<unsigned long long>(vmarea.file_offset));
	query.bindValue(":mm_raw_id", reinterpret_cast<unsigned long long>(vmarea.mm_id));

	if(!query.exec()){
		QSqlError err = query.lastError();
		std::cerr << "Query error: ";
		std::cerr << err.text().toStdString() << std::endl;
	}
}

static void process_data(zim32_page_table_entry &item, QSqlDatabase &db){
	QSqlQuery query;
	QString pte_entry_type;

	query.prepare("INSERT INTO page_table(entry_type, raw_value) VALUES(:type, :raw_value)");

	switch(item.type){
	case PGD:
		pte_entry_type = "PGD";
	case PUD:
			pte_entry_type = "PUD";
	case PMD:
			pte_entry_type = "PMD";
	case PTE:
			pte_entry_type = "PTE";
	}

	query.bindValue(":type", pte_entry_type);
	query.bindValue(":raw_value", static_cast<unsigned long long>(item.data));

	if(!query.exec()){
		QSqlError err = query.lastError();
		std::cerr << "Query error: ";
		std::cerr << err.text().toStdString() << std::endl;
	}

}



template<typename T>
bool load_data(FILE *f, long int *fpos, QSqlDatabase &db){
	size_t data_size = sizeof(T);
	T data{};
	size_t read_successfully;

	for(;;){
		fseek(f, *fpos, 0);

		std::cout << "Processing " << typeid(T).name() <<  " at offset " << *fpos << std::endl;

		read_successfully = fread(&data, data_size, 1, f);
		if(read_successfully != 1){
			std::cerr << "IO error\n";
			return false;
		}

		*fpos += data_size;

		if(is_structure_empty(&data, data_size) || feof(f) != 0){
			return true;
		}

		process_data(data, db);
	};
}



static bool flush_tables(){
	QSqlQuery query;
	if(
		query.exec("TRUNCATE TABLE zim32_vm.task") &&
		query.exec("TRUNCATE TABLE zim32_vm.mm") &&
		query.exec("TRUNCATE TABLE zim32_vm.page_table") &&
		query.exec("TRUNCATE TABLE zim32_vm.vm_area")
	){
		return true;
	}else{
		QSqlError err = query.lastError();
		std::cerr << err.text().toStdString() << std::endl;
		return false;
	}

}


int main (int, char **){

	FILE *f;
	long int fpos = 0;


	QSqlDatabase db = QSqlDatabase::addDatabase("QMYSQL");
	db.setHostName("localhost");
	db.setUserName("zim32");
	db.setPassword("sikkens");
	db.setDatabaseName("zim32_vm");
	if(!db.open()){
		std::cerr << "Can not open database connection\n";
		return 1;
	}

	f = fopen("/root/tmp", "rb");
	if(!f){
		std::cerr << "Can not open file\n";
		return 1;
	}

	if(!flush_tables()){
		std::cerr << "Can not flush tables\n";
		goto close_file_and_out;
	}


	if(!load_data<zim32_task_t>(f, &fpos, db)){
		goto close_file_and_out;
	}

	if(!load_data<zim32_mm_t>(f, &fpos, db)){
		goto close_file_and_out;
	}

	if(!load_data<zim32_vm_area_t>(f, &fpos, db)){
		goto close_file_and_out;
	}

	if(!load_data<zim32_page_table_entry>(f, &fpos, db)){
		goto close_file_and_out;
	}


close_file_and_out:
	if(db.isOpen()){
		db.close();
	}
	fclose(f);
	return 0;

}
