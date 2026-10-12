"""Black-box integration tests. Each test uses disposable data, never the demo DB.
Run: python tests/test_app.py build/library.exe
"""
import csv
import io
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
EXE = Path(sys.argv.pop(1)).resolve() if len(sys.argv) > 1 else ROOT / "build/library.exe"


class LibraryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.folder = Path(self.temp.name)
        self.db = self.folder / "library.db"

    def tearDown(self):
        self.temp.cleanup()

    def demo(self):
        shutil.copyfile(ROOT / "data/library.db", self.db)

    def run_app(self, commands=(), date="2026-10-12", status=0):
        result = subprocess.run(
            [str(EXE), "--data", str(self.db), "--today", date],
            input="\n".join(map(str, commands)) + ("\n" if commands else ""),
            text=True, capture_output=True, cwd=self.folder, timeout=8,
        )
        self.assertEqual(result.returncode, status, result.stdout + result.stderr)
        return result.stdout + result.stderr

    def basic(self, copies=1):
        self.run_app([1,3,"Test Book","Test Author","Science",copies,0,
                      2,3,"Member One","one@example.com",3,"Member Two","two@example.com",0,0])

    def test_demo_dashboard_and_case_insensitive_search(self):
        self.demo()
        out = self.run_app([4,1,2,0,1,2,"DATABASE",0,0])
        for text in ["Book titles: 8", "Total copies: 15", "Available copies: 12",
                     "Active loans: 3", "Overdue loans: 1", "Estimated overdue fines: PKR 80",
                     "Historical assessed fines: PKR 20", "Database Design Workbook"]:
            self.assertIn(text, out)

    def test_add_edit_quotes_and_persistence(self):
        self.basic()
        out = self.run_app([1,4,1,'Book "Revised"',"Author","Computing",2,1,0,
                            2,4,1,"Updated Member","new@example.com",1,0,0])
        self.assertIn("Book updated.", out)
        self.assertIn("Member updated.", out)
        out = self.run_app([1,1,0,2,1,0,0])
        self.assertIn('Book "Revised"', out)
        self.assertIn("Updated Member", out)
        self.assertTrue(Path(str(self.db)+".bak").exists())

    def test_issue_no_stock_return_and_double_return(self):
        self.basic()
        out = self.run_app([3,1,1,1,1,1,2,1,2,1,1,2,1,0,0])
        self.assertIn("Book issued.", out)
        self.assertIn("No copies available.", out)
        self.assertIn("Book returned.", out)
        self.assertIn("already been returned", out)
        out = self.run_app([1,1,0,3,5,0,0])
        self.assertIn("1/1", out)
        self.assertIn("Returned: 2026-10-12", out)

    def test_renewal_rules_and_overdue_return_fine(self):
        self.demo()
        out = self.run_app([3,3,1,3,2,1,3,2,2,1,1,0,0])
        self.assertIn("Overdue loans cannot be renewed", out)
        self.assertIn("New due date: 2026-11-02", out)
        self.assertIn("Renewal limit reached", out)
        self.assertIn("Final assessed fine: PKR 80", out)
        out = self.run_app([3,5,0,0], date="2026-10-20")
        self.assertIn("Final assessed fine: PKR 80", out)

    def test_cancel_edit_delete_issue_and_return(self):
        self.basic()
        before = self.db.read_bytes()
        out = self.run_app([1,4,1,"Cancelled","Author","Category",2,0,5,1,0,0,
                            3,1,1,1,0,0,0])
        self.assertGreaterEqual(out.count("Cancelled."), 3)
        self.assertEqual(before, self.db.read_bytes())
        self.run_app([3,1,1,1,1,0,0])
        before = self.db.read_bytes()
        self.run_app([3,2,1,0,0,0])
        self.assertEqual(before, self.db.read_bytes())

    def test_deletion_and_persistent_id_counters(self):
        self.basic()
        self.run_app([1,5,1,1,0,2,5,2,1,0,0])
        out = self.run_app([1,3,"New Book","Author","Category",1,0,
                            2,3,"New Member","new@example.com",0,0])
        self.assertIn("Book added with ID 2", out)
        self.assertIn("Member registered with ID 3", out)

    def test_history_protection_and_missing_ids(self):
        self.demo()
        out = self.run_app([1,5,1,4,999,0,2,5,1,4,999,0,3,1,999,2,999,0,0])
        self.assertIn("Cannot delete a book with loan history", out)
        self.assertIn("Cannot delete a member with loan history", out)
        self.assertIn("Book not found", out)
        self.assertIn("Member not found", out)
        self.assertIn("Loan not found", out)

    def test_strict_numbers_and_eof(self):
        out = self.run_app(["abc","2abc","1.5",99,0])
        self.assertEqual(out.count("Enter a whole number"), 4)
        out = self.run_app([])
        self.assertIn("Input closed", out)
        out = self.run_app([1,3,"Partial book"])
        self.assertIn("Input closed", out)
        self.assertFalse(self.db.exists())

    def test_invalid_database_is_not_overwritten(self):
        self.db.write_text("LIBRARY_DB 1\nNEXT 2 1 1\nBOOK 1 bad\nEND\n")
        before = self.db.read_bytes()
        out = self.run_app([0], status=1)
        self.assertIn("Malformed database", out)
        self.assertEqual(before, self.db.read_bytes())

    def test_save_failure_rolls_back_in_memory(self):
        self.basic()
        before = self.db.read_bytes()
        Path(str(self.db)+".tmp").mkdir()
        out = self.run_app([1,3,"Unsaved","Author","Category",1,4,1,0,0])
        self.assertIn("Operation failed", out)
        self.assertIn("Book titles: 1", out)
        self.assertEqual(before, self.db.read_bytes())

    def test_exports_and_formula_neutralization(self):
        self.run_app([1,3,"=1+1","Author","Category",1,0,4,4,0,0])
        rows = list(csv.reader(io.StringIO((self.folder/"exports/books.csv").read_text())))
        self.assertEqual(rows[1][1], "'=1+1")
        self.assertTrue((self.folder/"exports/members.csv").exists())
        self.assertTrue((self.folder/"exports/loans.csv").exists())

    def test_leap_year_date_arithmetic(self):
        self.basic()
        out = self.run_app([3,1,1,1,1,0,0], date="2028-02-20")
        self.assertIn("Due: 2028-03-05", out)
        out = self.run_app([0], date="2027-02-29", status=1)
        self.assertIn("Invalid date", out)

    def test_borrowing_limit_and_duplicate_loan(self):
        commands = [1]
        for index in range(6):
            commands += [3, f"Book {index+1}", "Author", "Category", 2]
        commands += [0,2,3,"Member","member@example.com",0,3,1,1,1,1,
                     1,1,1]
        for book in range(2,6):
            commands += [1,1,book,1]
        commands += [1,1,0,0]
        out = self.run_app(commands)
        self.assertIn("already has this book", out)
        self.assertIn("reached the borrowing limit", out)


if __name__ == "__main__":
    if not EXE.is_file():
        raise SystemExit(f"Build the application first; executable missing: {EXE}")
    unittest.main(verbosity=2)
