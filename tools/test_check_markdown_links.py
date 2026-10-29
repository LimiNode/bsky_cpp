import tempfile
import unittest
from pathlib import Path

from check_markdown_links import find_failures


class MarkdownLinkCheckerTests(unittest.TestCase):
    def test_prunes_generated_and_dependency_trees_but_checks_external_readme(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "README.md").write_text("# root\n", encoding="utf-8")
            (root / "docs").mkdir()
            (root / "docs" / "guide.md").write_text(
                "[root](../README.md)\n", encoding="utf-8"
            )
            (root / "build" / "generated").mkdir(parents=True)
            (root / "build" / "generated" / "broken.md").write_text(
                "[missing](nope.md)\n", encoding="utf-8"
            )
            (root / "external" / "dependency").mkdir(parents=True)
            (root / "external" / "README.md").write_text(
                "[root](../README.md)\n", encoding="utf-8"
            )
            (root / "external" / "dependency" / "broken.md").write_text(
                "[missing](nope.md)\n", encoding="utf-8"
            )

            self.assertEqual(find_failures(root), [])

            (root / "external" / "README.md").write_text(
                "[missing](nope.md)\n", encoding="utf-8"
            )
            self.assertEqual(find_failures(root), ["external/README.md -> nope.md"])


if __name__ == "__main__":
    unittest.main()
