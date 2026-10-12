// Library Management System - C++17
// All sample identities are fictional. See README.md for build and usage instructions.
#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;
namespace fs = std::filesystem;

constexpr int LOAN_DAYS = 14;
constexpr int MAX_ACTIVE_LOANS = 5;
constexpr int DAILY_FINE_PKR = 10;
constexpr int MAX_RENEWALS = 1;
constexpr int MAX_COPIES = 10000;

struct EndOfInput {};

string trim(const string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == string::npos) return "";
    return value.substr(first, value.find_last_not_of(" \t\r\n") - first + 1);
}

string lower(string value) {
    // Cast to unsigned char before cctype calls to avoid undefined behavior.
    for (char& ch : value) ch = static_cast<char>(tolower(static_cast<unsigned char>(ch)));
    return value;
}

bool validText(const string& value, size_t maximum) {
    if (trim(value).empty() || value.size() > maximum) return false;
    return none_of(value.begin(), value.end(), [](unsigned char ch) { return ch < 32 || ch == 127; });
}

string readLine(const string& prompt) {
    cout << prompt;
    string line;
    if (!getline(cin, line)) throw EndOfInput{};
    return trim(line);
}

int readInt(const string& prompt, int minimum, int maximum) {
    while (true) {
        string line = readLine(prompt);
        istringstream input(line);
        long long value;
        char extra;
        // Reading the complete line rejects 12abc and 12.5, and handles overflow.
        if ((input >> value) && !(input >> extra) && value >= minimum && value <= maximum)
            return static_cast<int>(value);
        cout << "Enter a whole number from " << minimum << " to " << maximum << ".\n";
    }
}

string readText(const string& prompt, size_t maximum = 120) {
    while (true) {
        string value = readLine(prompt);
        if (validText(value, maximum)) return value;
        cout << "Enter nonempty text (up to " << maximum << " characters; no control characters).\n";
    }
}

bool confirm(const string& prompt) {
    return readInt(prompt + " [1 = Yes, 0 = Cancel]: ", 0, 1) == 1;
}

// Gregorian dates use integer day numbers. Calendar arithmetic does not depend
// on daylight saving time, so a loan lasts exactly LOAN_DAYS calendar days.
class Date {
    int dayNumber_ = 0;
    static bool leap(int year) { return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0); }
    static int monthDays(int year, int month) {
        static const int days[] = {31,28,31,30,31,30,31,31,30,31,30,31};
        return days[month - 1] + (month == 2 && leap(year) ? 1 : 0);
    }
public:
    static Date parse(const string& value) {
        if (value.size() != 10 || value[4] != '-' || value[7] != '-')
            throw runtime_error("Date must use YYYY-MM-DD.");
        for (size_t i = 0; i < value.size(); ++i)
            if (i != 4 && i != 7 && !isdigit(static_cast<unsigned char>(value[i])))
                throw runtime_error("Date contains invalid characters.");
        int year = stoi(value.substr(0,4)), month = stoi(value.substr(5,2)), day = stoi(value.substr(8,2));
        if (year < 1900 || year > 2200 || month < 1 || month > 12 || day < 1 || day > monthDays(year, month))
            throw runtime_error("Invalid date; supported years are 1900-2200.");
        Date result;
        for (int y = 1900; y < year; ++y) result.dayNumber_ += leap(y) ? 366 : 365;
        for (int m = 1; m < month; ++m) result.dayNumber_ += monthDays(year, m);
        result.dayNumber_ += day - 1;
        return result;
    }
    static Date today() {
        time_t now = time(nullptr);
        tm* local = localtime(&now);
        if (!local) throw runtime_error("Cannot read the system date.");
        ostringstream text;
        text << put_time(local, "%Y-%m-%d");
        return parse(text.str());
    }
    string str() const {
        int remaining = dayNumber_, year = 1900, month = 1;
        while (remaining >= (leap(year) ? 366 : 365)) remaining -= leap(year++) ? 366 : 365;
        while (remaining >= monthDays(year, month)) remaining -= monthDays(year, month++);
        ostringstream out;
        out << setfill('0') << setw(4) << year << '-' << setw(2) << month << '-' << setw(2) << remaining + 1;
        return out.str();
    }
    Date add(int days) const {
        Date result = *this;
        result.dayNumber_ += days;
        // Round-trip validation enforces the documented date range.
        return parse(result.str());
    }
    int operator-(const Date& other) const { return dayNumber_ - other.dayNumber_; }
};

class Book {
public:
    int id = 0;
    string title, author, category;
    int copies = 1;
};

class Member {
public:
    int id = 0;
    string name, contact;
};

class Loan {
public:
    int id = 0, bookId = 0, memberId = 0;
    string issued, due, returned = "-";
    int renewals = 0;
    bool active() const { return returned == "-"; }
};

struct Database {
    // Counters are persisted independently: deleting the highest ID never reuses it.
    int nextBook = 1, nextMember = 1, nextLoan = 1;
    vector<Book> books;
    vector<Member> members;
    vector<Loan> loans;
};

template<class T> T* findId(vector<T>& items, int id) {
    for (T& item : items) if (item.id == id) return &item;
    return nullptr;
}
template<class T> const T* findId(const vector<T>& items, int id) {
    for (const T& item : items) if (item.id == id) return &item;
    return nullptr;
}

int activeForBook(const Database& db, int id) {
    return static_cast<int>(count_if(db.loans.begin(), db.loans.end(), [id](const Loan& l) { return l.active() && l.bookId == id; }));
}
int activeForMember(const Database& db, int id) {
    return static_cast<int>(count_if(db.loans.begin(), db.loans.end(), [id](const Loan& l) { return l.active() && l.memberId == id; }));
}

template<class T> void validateIds(const vector<T>& items, int next) {
    set<int> ids;
    if (next <= 0) throw runtime_error("Invalid next-ID counter.");
    for (const T& item : items)
        if (item.id <= 0 || item.id >= next || !ids.insert(item.id).second)
            throw runtime_error("Invalid, duplicate, or reused ID in database.");
}

void validate(const Database& db) {
    validateIds(db.books, db.nextBook); validateIds(db.members, db.nextMember); validateIds(db.loans, db.nextLoan);
    for (const Book& b : db.books) {
        if (!validText(b.title,120) || !validText(b.author,120) || !validText(b.category,60) ||
            b.copies < 1 || b.copies > MAX_COPIES || activeForBook(db,b.id) > b.copies)
            throw runtime_error("Invalid book record or copy count.");
    }
    for (const Member& m : db.members) {
        if (!validText(m.name,120) || !validText(m.contact,120) || activeForMember(db,m.id) > MAX_ACTIVE_LOANS)
            throw runtime_error("Invalid member record or borrowing limit.");
    }
    for (const Loan& l : db.loans) {
        if (!findId(db.books,l.bookId) || !findId(db.members,l.memberId))
            throw runtime_error("Loan refers to a missing book or member.");
        Date issued = Date::parse(l.issued), due = Date::parse(l.due);
        if (l.renewals < 0 || l.renewals > MAX_RENEWALS || due - issued != LOAN_DAYS * (1 + l.renewals))
            throw runtime_error("Invalid loan duration or renewal count.");
        if (!l.active() && Date::parse(l.returned) - issued < 0)
            throw runtime_error("Return date precedes issue date.");
    }
}

Database load(const fs::path& path) {
    if (!fs::exists(path)) {
        if (fs::exists(path.string()+".bak"))
            throw runtime_error("Database is missing but a .bak exists. Restore that backup before starting.");
        return {};
    }
    ifstream file(path);
    if (!file) throw runtime_error("Cannot open database: " + path.string());
    string line;
    if (!getline(file,line) || trim(line) != "LIBRARY_DB 1") throw runtime_error("Invalid database header.");
    Database db;
    bool counters = false, ended = false;
    int number = 1;
    while (getline(file,line)) {
        ++number;
        if (trim(line).empty()) continue;
        if (ended) throw runtime_error("Unexpected content after END.");
        istringstream row(line);
        string tag, extra;
        row >> tag;
        bool good = false;
        if (tag == "NEXT" && !counters) {
            good = static_cast<bool>(row >> db.nextBook >> db.nextMember >> db.nextLoan); counters = true;
        } else if (tag == "BOOK") {
            Book b;
            good = static_cast<bool>(row >> b.id >> quoted(b.title) >> quoted(b.author) >> quoted(b.category) >> b.copies);
            db.books.push_back(b);
        } else if (tag == "MEMBER") {
            Member m;
            good = static_cast<bool>(row >> m.id >> quoted(m.name) >> quoted(m.contact)); db.members.push_back(m);
        } else if (tag == "LOAN") {
            Loan l;
            good = static_cast<bool>(row >> l.id >> l.bookId >> l.memberId >> l.issued >> l.due >> l.returned >> l.renewals);
            db.loans.push_back(l);
        } else if (tag == "END") { good = true; ended = true; }
        if (!good || (row >> extra)) throw runtime_error("Malformed database record at line " + to_string(number));
    }
    if (file.bad() || !counters || !ended) throw runtime_error("Incomplete database; no records were overwritten.");
    validate(db);
    return db;
}

void save(const Database& db, const fs::path& path) {
    validate(db);
    if (!path.parent_path().empty()) fs::create_directories(path.parent_path());
    fs::path temporary = path.string()+".tmp", backup = path.string()+".bak";
    ofstream file(temporary, ios::trunc);
    if (!file) throw runtime_error("Cannot create temporary database file.");
    file << "LIBRARY_DB 1\nNEXT " << db.nextBook << ' ' << db.nextMember << ' ' << db.nextLoan << '\n';
    for (const Book& b : db.books)
        file << "BOOK " << b.id << ' ' << quoted(b.title) << ' ' << quoted(b.author) << ' ' << quoted(b.category) << ' ' << b.copies << '\n';
    for (const Member& m : db.members)
        file << "MEMBER " << m.id << ' ' << quoted(m.name) << ' ' << quoted(m.contact) << '\n';
    for (const Loan& l : db.loans)
        file << "LOAN " << l.id << ' ' << l.bookId << ' ' << l.memberId << ' ' << l.issued << ' ' << l.due << ' ' << l.returned << ' ' << l.renewals << '\n';
    file << "END\n";
    file.close();
    if (!file) throw runtime_error("Writing the database failed; original data is unchanged.");
    // Keep the previous successful database as a recovery copy before replacement.
    bool hadOriginal = fs::exists(path);
    if (hadOriginal) {
        fs::copy_file(path,backup,fs::copy_options::overwrite_existing);
        fs::remove(path);
    }
    try { fs::rename(temporary,path); }
    catch (...) {
        if (hadOriginal) fs::copy_file(backup,path,fs::copy_options::overwrite_existing);
        throw;
    }
}

string shortText(const string& value, size_t width) {
    return value.size() <= width ? value : value.substr(0,width-3)+"...";
}

string csv(const string& value) {
    // Neutralize spreadsheet formulas in user-controlled text when exporting CSV.
    string safe = value;
    if (!safe.empty() && string("=+-@").find(safe[0]) != string::npos) safe = "'" + safe;
    string result = "\"";
    for (char ch : safe) { if (ch == '"') result += '"'; result += ch; }
    return result + '"';
}

class Library {
    Database db_;
    fs::path path_;
    string fixedToday_;
    Date today() const { return fixedToday_.empty() ? Date::today() : Date::parse(fixedToday_); }
    int overdueDays(const Loan& l) const {
        Date end = l.active() ? today() : Date::parse(l.returned);
        return max(0,end-Date::parse(l.due));
    }
    void commit(Database candidate, const string& message) {
        // Apply changes only after a successful save, so failures do not leave
        // the in-memory collection ahead of the on-disk database.
        save(candidate,path_);
        db_ = move(candidate);
        cout << message << " Saved successfully.\n";
    }
    static int takeId(int& next) {
        if (next == numeric_limits<int>::max()) throw runtime_error("No more IDs available.");
        return next++;
    }
    void listBooks(const string& query = "") const {
        cout << left << setw(6) << "ID" << setw(33) << "Title" << setw(23) << "Author" << setw(18) << "Category" << "Free/Total\n";
        int shown = 0;
        for (const Book& b : db_.books) {
            if (!query.empty() && lower(b.title+" "+b.author+" "+b.category+" "+to_string(b.id)).find(lower(query)) == string::npos) continue;
            cout << left << setw(6) << b.id << setw(33) << shortText(b.title,31) << setw(23) << shortText(b.author,21)
                 << setw(18) << shortText(b.category,16) << b.copies-activeForBook(db_,b.id) << '/' << b.copies << '\n';
            ++shown;
        }
        cout << shown << " book title(s) displayed.\n";
    }
    void listMembers(const string& query = "") const {
        cout << left << setw(6) << "ID" << setw(30) << "Name" << setw(36) << "Contact" << "Active loans\n";
        int shown = 0;
        for (const Member& m : db_.members) {
            if (!query.empty() && lower(m.name+" "+m.contact+" "+to_string(m.id)).find(lower(query)) == string::npos) continue;
            cout << left << setw(6) << m.id << setw(30) << shortText(m.name,28) << setw(36) << shortText(m.contact,34) << activeForMember(db_,m.id) << '\n';
            ++shown;
        }
        cout << shown << " member(s) displayed.\n";
    }
    void listLoans(bool activeOnly, bool overdueOnly = false, int memberId = 0) const {
        int shown = 0;
        for (const Loan& l : db_.loans) {
            if ((activeOnly && !l.active()) || (overdueOnly && (!l.active() || overdueDays(l)==0)) || (memberId && l.memberId != memberId)) continue;
            const Book* b = findId(db_.books,l.bookId);
            const Member* m = findId(db_.members,l.memberId);
            cout << "Loan #" << l.id << " | Book #" << b->id << ": " << b->title << " | Member #" << m->id << ": " << m->name << '\n';
            cout << "Issued: " << l.issued << " | Due: " << l.due << " | Returned: " << l.returned << " | Renewals: " << l.renewals << '\n';
            cout << (l.active() ? "Estimated fine" : "Final assessed fine") << ": PKR " << 1LL*overdueDays(l)*DAILY_FINE_PKR << "\n---\n";
            ++shown;
        }
        cout << shown << " loan(s) displayed.\n";
    }
    void bookAction(int option) {
        if (option==1) { listBooks(); return; }
        if (option==2) { listBooks(readText("Search title, author, category, or ID: ")); return; }
        Database candidate = db_;
        if (option==3) {
            Book b;
            b.title=readText("Title: "); b.author=readText("Author: "); b.category=readText("Category: ",60);
            b.copies=readInt("Total copies: ",1,MAX_COPIES);
            b.id=takeId(candidate.nextBook); candidate.books.push_back(b);
            commit(move(candidate),"Book added with ID " + to_string(b.id) + "."); return;
        }
        int id=readInt("Book ID: ",1,numeric_limits<int>::max());
        Book* b=findId(candidate.books,id);
        if (!b) { cout << "Book not found.\n"; return; }
        if (option==4) {
            cout << "Editing: " << b->title << ". Re-enter all fields.\n";
            b->title=readText("Title: "); b->author=readText("Author: "); b->category=readText("Category: ",60);
            b->copies=readInt("Total copies: ",max(1,activeForBook(db_,id)),MAX_COPIES);
            if (confirm("Save book changes?")) commit(move(candidate),"Book updated.");
            else cout << "Cancelled.\n";
        } else {
            if (any_of(db_.loans.begin(),db_.loans.end(),[id](const Loan& l){return l.bookId==id;})) {
                cout << "Cannot delete a book with loan history; its records must remain available.\n"; return;
            }
            if (confirm("Delete '"+b->title+"'?")) {
                candidate.books.erase(remove_if(candidate.books.begin(),candidate.books.end(),[id](const Book& item){return item.id==id;}),candidate.books.end());
                commit(move(candidate),"Book deleted.");
            } else cout << "Cancelled.\n";
        }
    }
    void memberAction(int option) {
        if (option==1) { listMembers(); return; }
        if (option==2) { listMembers(readText("Search member name, contact, or ID: ")); return; }
        Database candidate=db_;
        if (option==3) {
            Member m;
            m.name=readText("Name: "); m.contact=readText("Contact (email or phone): ");
            m.id=takeId(candidate.nextMember); candidate.members.push_back(m);
            commit(move(candidate),"Member registered with ID "+to_string(m.id)+"."); return;
        }
        int id=readInt("Member ID: ",1,numeric_limits<int>::max());
        Member* m=findId(candidate.members,id);
        if (!m) { cout << "Member not found.\n"; return; }
        if (option==4) {
            cout << "Editing: " << m->name << ". Re-enter all fields.\n";
            m->name=readText("Name: "); m->contact=readText("Contact: ");
            if (confirm("Save member changes?")) commit(move(candidate),"Member updated.");
            else cout << "Cancelled.\n";
        } else {
            if (any_of(db_.loans.begin(),db_.loans.end(),[id](const Loan& l){return l.memberId==id;})) {
                cout << "Cannot delete a member with loan history.\n"; return;
            }
            if (confirm("Delete member '"+m->name+"'?")) {
                candidate.members.erase(remove_if(candidate.members.begin(),candidate.members.end(),[id](const Member& item){return item.id==id;}),candidate.members.end());
                commit(move(candidate),"Member deleted.");
            } else cout << "Cancelled.\n";
        }
    }
    void issue() {
        int memberId=readInt("Member ID: ",1,numeric_limits<int>::max());
        if (!findId(db_.members,memberId)) { cout << "Member not found.\n"; return; }
        if (activeForMember(db_,memberId)>=MAX_ACTIVE_LOANS) { cout << "Member has reached the borrowing limit.\n"; return; }
        int bookId=readInt("Book ID: ",1,numeric_limits<int>::max());
        const Book* b=findId(db_.books,bookId);
        if (!b) { cout << "Book not found.\n"; return; }
        if (activeForBook(db_,bookId)>=b->copies) { cout << "No copies available.\n"; return; }
        for (const Loan& l:db_.loans) if(l.active() && l.bookId==bookId && l.memberId==memberId) {
            cout << "This member already has this book.\n"; return;
        }
        Loan l;
        l.bookId=bookId; l.memberId=memberId; l.issued=today().str(); l.due=today().add(LOAN_DAYS).str();
        cout << "Selected: " << b->title << " | Due: " << l.due << '\n';
        if (!confirm("Issue this book?")) { cout << "Cancelled.\n"; return; }
        Database candidate=db_; l.id=takeId(candidate.nextLoan); candidate.loans.push_back(l);
        commit(move(candidate),"Book issued. Loan ID "+to_string(l.id)+".");
    }
    void changeLoan(bool renewal) {
        int id=readInt("Loan ID: ",1,numeric_limits<int>::max());
        Database candidate=db_;
        Loan* l=findId(candidate.loans,id);
        if (!l) { cout << "Loan not found.\n"; return; }
        if (!l->active()) { cout << "This loan has already been returned.\n"; return; }
        if (today()-Date::parse(l->issued)<0) { cout << "Current date precedes the issue date; check your clock.\n"; return; }
        if (renewal) {
            if (l->renewals>=MAX_RENEWALS) { cout << "Renewal limit reached.\n"; return; }
            if (overdueDays(*l)>0) { cout << "Overdue loans cannot be renewed.\n"; return; }
            l->due=Date::parse(l->due).add(LOAN_DAYS).str(); ++l->renewals;
            cout << "New due date: " << l->due << '\n';
            if (!confirm("Renew this loan?")) { cout << "Cancelled.\n"; return; }
            commit(move(candidate),"Loan renewed.");
        } else {
            l->returned=today().str();
            cout << "Final assessed fine: PKR " << 1LL*overdueDays(*l)*DAILY_FINE_PKR << '\n';
            if (!confirm("Return this book?")) { cout << "Cancelled.\n"; return; }
            commit(move(candidate),"Book returned.");
        }
    }
    void dashboard() const {
        long long copies=0, active=0, overdue=0, estimate=0, assessed=0;
        for(const Book& b:db_.books) copies+=b.copies;
        for(const Loan& l:db_.loans) {
            if(l.active()) { ++active; if(overdueDays(l)>0) ++overdue; estimate+=1LL*overdueDays(l)*DAILY_FINE_PKR; }
            else assessed+=1LL*overdueDays(l)*DAILY_FINE_PKR;
        }
        cout << "\nSUMMARY AS OF " << today().str() << "\nBook titles: " << db_.books.size()
             << " | Total copies: " << copies << " | Available copies: " << copies-active
             << "\nMembers: " << db_.members.size() << " | Active loans: " << active << " | Overdue loans: " << overdue
             << "\nEstimated overdue fines: PKR " << estimate << "\nHistorical assessed fines: PKR " << assessed << '\n';
        cout << "Fine amounts are assessments only; this application does not record payments.\n";
    }
    void exportCsv() const {
        fs::path directory=path_.parent_path()/"exports";
        vector<string> names={"books.csv","members.csv","loans.csv"};
        bool existing=false;
        for(const string& name:names) if(fs::exists(directory/name)) existing=true;
        if(existing && !confirm("Replace existing CSV exports?")) { cout << "Cancelled.\n"; return; }
        fs::create_directories(directory);
        auto write=[&](const string& name,const string& content) {
            ofstream out(directory/name,ios::binary); out << content; out.close();
            if(!out) throw runtime_error("Could not write CSV: "+name);
        };
        ostringstream books,members,loans;
        books << "id,title,author,category,total_copies,available_copies\n";
        for(const Book& b:db_.books) books << b.id << ',' << csv(b.title) << ',' << csv(b.author) << ',' << csv(b.category) << ',' << b.copies << ',' << b.copies-activeForBook(db_,b.id) << '\n';
        members << "id,name,contact,active_loans\n";
        for(const Member& m:db_.members) members << m.id << ',' << csv(m.name) << ',' << csv(m.contact) << ',' << activeForMember(db_,m.id) << '\n';
        loans << "id,book_id,member_id,issued,due,returned,renewals,assessed_or_estimated_fine_pkr\n";
        for(const Loan& l:db_.loans) loans << l.id << ',' << l.bookId << ',' << l.memberId << ',' << l.issued << ',' << l.due << ',' << l.returned << ',' << l.renewals << ',' << 1LL*overdueDays(l)*DAILY_FINE_PKR << '\n';
        write(names[0],books.str()); write(names[1],members.str()); write(names[2],loans.str());
        cout << "CSV files exported to " << directory.string() << '\n';
    }
public:
    Library(fs::path path,string fixed):db_(load(path)),path_(move(path)),fixedToday_(move(fixed)) {}
    void run() {
        cout << "\nLIBRARY MANAGEMENT SYSTEM\nDatabase: " << fs::absolute(path_).string() << '\n';
        if(!fixedToday_.empty()) cout << "Demo date override: " << fixedToday_ << '\n';
        while(true) {
            try {
                cout << "\n1. Books\n2. Members\n3. Issue / return / renew\n4. Reports\n0. Exit\n";
                int mainChoice=readInt("Choose: ",0,4);
                if(mainChoice==0) { cout << "Goodbye! All confirmed changes are saved.\n"; return; }
                bool back=false;
                while(!back) {
                    if(mainChoice==1) {
                        cout << "\nBOOKS\n1. List\n2. Search\n3. Add\n4. Edit\n5. Delete\n0. Back\n";
                        int option=readInt("Choose: ",0,5); if(!option) back=true; else bookAction(option);
                    } else if(mainChoice==2) {
                        cout << "\nMEMBERS\n1. List\n2. Search\n3. Register\n4. Edit\n5. Delete\n0. Back\n";
                        int option=readInt("Choose: ",0,5); if(!option) back=true; else memberAction(option);
                    } else if(mainChoice==3) {
                        cout << "\nCIRCULATION\n1. Issue\n2. Return\n3. Renew\n4. Active loans\n5. All loan history\n0. Back\n";
                        int option=readInt("Choose: ",0,5);
                        if(!option) back=true; else if(option==1) issue(); else if(option==2 || option==3) changeLoan(option==3); else listLoans(option==4);
                    } else {
                        cout << "\nREPORTS\n1. Dashboard\n2. Overdue loans\n3. Member loan history\n4. Export CSV\n0. Back\n";
                        int option=readInt("Choose: ",0,4);
                        if(!option) back=true; else if(option==1) dashboard(); else if(option==2) listLoans(true,true);
                        else if(option==3) {
                            int id=readInt("Member ID: ",1,numeric_limits<int>::max());
                            if(!findId(db_.members,id)) cout << "Member not found.\n"; else listLoans(false,false,id);
                        } else exportCsv();
                    }
                }
            } catch(const exception& error) { cout << "Operation failed: " << error.what() << "\nReturning to main menu.\n"; }
        }
    }
};

int main(int argc,char* argv[]) {
    fs::path path="data/library.db";
    string fixed;
    try {
        for(int i=1;i<argc;++i) {
            string option=argv[i];
            if(option=="--help") {
                cout << "Usage: library [--data PATH] [--today YYYY-MM-DD]\n"
                     << "Default database: data/library.db relative to the working folder.\n"
                     << "--today sets a fixed date for demos/tests; omit for normal local time.\n"; return 0;
            }
            if((option=="--data" || option=="--today") && i+1<argc) {
                string value=argv[++i];
                if(option=="--data") { if(value.empty()) throw runtime_error("Empty database path."); path=value; }
                else { Date::parse(value); fixed=value; }
            } else throw runtime_error("Unknown or incomplete option: "+option);
        }
        Library app(path,fixed); app.run();
    } catch(const EndOfInput&) { cout << "\nInput closed. Last confirmed changes are saved.\n"; }
    catch(const exception& error) { cerr << "Error: " << error.what() << '\n'; return 1; }
    return 0;
}
