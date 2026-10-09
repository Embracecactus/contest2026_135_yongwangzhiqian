#!/usr/bin/env python3
"""Contract for the untrusted GitHub event to pinned-source CI boundary."""
import importlib.util
import hashlib
import json
import subprocess
import tempfile
import unittest
from pathlib import Path
from xml.etree import ElementTree


ROOT = Path(__file__).resolve().parents[3]
MODULE = ROOT / ".github/scripts/shaniu_source.py"
TEAM = "Embracecactus/contest2026_135_yongwangzhiqian"
SHA = "a" * 40
BASE = "b" * 40
HEAD = "c" * 40


def source_module():
    spec = importlib.util.spec_from_file_location("shaniu_source", MODULE)
    module = importlib.util.module_from_spec(spec)
    assert spec and spec.loader
    spec.loader.exec_module(module)
    return module


def env(**extra):
    value = {
        "GITHUB_REPOSITORY": TEAM,
        "GITHUB_SHA": SHA,
        "GITHUB_REF": "refs/heads/dev-ai-contest-2026",
    }
    value.update(extra)
    return value


def git(directory, *args):
    return subprocess.run(["git", "-C", str(directory), *args], check=True,
                          capture_output=True, text=True).stdout.strip()


def commit(directory, name):
    (directory / name).write_text(name)
    git(directory, "add", name)
    git(directory, "-c", "user.name=ci", "-c", "user.email=ci@example.invalid",
        "commit", "-m", name)
    return git(directory, "rev-parse", "HEAD")


class SourceResolutionTest(unittest.TestCase):
    def resolve(self, event, **environment):
        environment.setdefault("GITHUB_EVENT_NAME", event["event_name"])
        return source_module().resolve_source(env(**environment), event)

    def assert_identity(self, actual, *, event, repo, candidate, head=None,
                        base=None, head_repo=None):
        self.assertEqual(actual["schema"], 1)
        self.assertEqual(actual["event"], event)
        self.assertEqual(actual["repository"], repo)
        self.assertEqual(actual["repository_url"], f"https://github.com/{repo}.git")
        self.assertEqual(actual["candidate_sha"], candidate)
        self.assertEqual(actual["source_ref"], candidate)
        self.assertEqual(actual["head_sha"], head or candidate)
        self.assertEqual(actual["base_sha"], base)
        self.assertEqual(actual["head_repository"], head_repo)

    def test_push_and_dispatch_are_exact_environment_sha(self):
        for kind, payload in (("push", {"after": SHA}), ("workflow_dispatch", {})):
            with self.subTest(kind=kind):
                got = self.resolve({"event_name": kind, **payload})
                self.assert_identity(got, event=kind, repo=TEAM, candidate=SHA)

    def test_fork_push_is_pinned_to_its_environment_repository(self):
        fork = "someone/contest-fork"
        got = self.resolve({"event_name": "push", "after": SHA},
                           GITHUB_REPOSITORY=fork)
        self.assert_identity(got, event="push", repo=fork, candidate=SHA)

    def test_push_rejects_after_mismatch_or_untrusted_text(self):
        for bad in (BASE, "a" * 39, SHA + "\nrefs/heads/pwn"):
            with self.subTest(bad=repr(bad)):
                with self.assertRaises(ValueError):
                    self.resolve({"event_name": "push", "after": bad})

    def test_dispatch_rejects_non_sha_repository_and_ref(self):
        for key, value in (("GITHUB_SHA", "a" * 39),
                           ("GITHUB_REPOSITORY", TEAM + "\nattacker/x"),
                           ("GITHUB_REF", "refs/heads/x\n--upload-pack=x")):
            with self.subTest(key=key):
                with self.assertRaises(ValueError):
                    self.resolve({"event_name": "workflow_dispatch"}, **{key: value})

    def test_pull_request_uses_verified_merge_candidate_and_base_repository(self):
        event = {"event_name": "pull_request", "number": 17,
                 "pull_request": {"merge_commit_sha": SHA,
                                  "head": {"sha": HEAD,
                                           "repo": {"full_name": "fork/voice"}},
                                  "base": {"sha": BASE,
                                           "repo": {"full_name": TEAM}}}}
        got = self.resolve(event, GITHUB_REF="refs/pull/17/merge")
        self.assertEqual(got["fetch_ref"], "refs/pull/17/merge")
        # synchronize payload may report an earlier computed merge; Actions SHA wins.
        event["pull_request"]["merge_commit_sha"] = BASE
        stale = self.resolve(event, GITHUB_REF="refs/pull/17/merge")
        self.assertEqual(stale["candidate_sha"], SHA)
        self.assertEqual(stale["reported_merge_sha"], BASE)
        self.assert_identity(got, event="pull_request", repo=TEAM, candidate=SHA,
                             head=HEAD, base=BASE, head_repo="fork/voice")

    def test_pull_request_rejects_target_wrong_ref_repo_and_number(self):
        base = {"event_name": "pull_request", "number": 17,
                "pull_request": {"merge_commit_sha": SHA,
                                 "head": {"sha": HEAD, "repo": {"full_name": "fork/voice"}},
                                 "base": {"sha": BASE, "repo": {"full_name": TEAM}}}}
        variants = [
            ({**base, "event_name": "pull_request_target"}, {}),
            (base, {"GITHUB_REF": "refs/heads/main"}),
            ({**base, "number": 18}, {}),
            ({**base, "pull_request": {**base["pull_request"],
                                         "base": {"sha": BASE, "repo": {"full_name": "other/repo"}}}}, {}),
        ]
        for event, environment in variants:
            with self.subTest(event=event["event_name"], environment=environment):
                with self.assertRaises(ValueError):
                    self.resolve(event, **environment)

    def test_post_merge_push_is_not_treated_as_pull_request(self):
        got = self.resolve({"event_name": "push", "after": SHA,
                            "pull_request": {"head": {"sha": HEAD}}})
        self.assert_identity(got, event="push", repo=TEAM, candidate=SHA)

    def test_override_has_only_team_extend_at_candidate_sha(self):
        identity = self.resolve({"event_name": "push", "after": SHA})
        with tempfile.TemporaryDirectory() as temporary:
            target = Path(temporary) / "local.xml"
            source_module().write_override(identity, target)
            root = ElementTree.parse(target).getroot()
            remotes = root.findall("remote")
            projects = root.findall("project")
            extends = root.findall("extend-project")
            self.assertEqual(len(remotes), 1)
            self.assertEqual(remotes[0].attrib, {
                "name": "shaniu-candidate", "fetch": "https://github.com/Embracecactus/",
            })
            self.assertEqual(projects, [])
            self.assertEqual(len(extends), 1)
            self.assertEqual(extends[0].attrib, {
                "name": "contest2026_135_yongwangzhiqian",
                "path": "contest2026_135_yongwangzhiqian",
                "remote": "shaniu-candidate", "revision": SHA, "upstream": "refs/heads/dev-ai-contest-2026",
            })

    def test_team_project_resolves_to_explicit_official_remote(self):
        def remotes(path):
            root = ElementTree.parse(path).getroot()
            values = {}
            for include in root.findall("include"):
                included, _ = remotes(path.parent / include.attrib["name"])
                values.update(included)
            values.update({remote.attrib["name"]: remote.attrib["fetch"]
                           for remote in root.findall("remote")})
            return values, root

        values, team = remotes(ROOT / "contest2026_135_yongwangzhiqian.xml")
        project = team.find("./project[@path='contest2026_135_yongwangzhiqian']")
        self.assertIsNotNone(project)
        self.assertEqual(values[project.attrib["remote"]], "https://github.com/open-vela/")

    def test_delivery_rejects_source_input_mismatch(self):
        identity = self.resolve({"event_name": "push", "after": SHA})
        with tempfile.TemporaryDirectory() as temporary:
            delivery = Path(temporary)
            archived = {**identity, "manifest_sha": SHA, "source_sha": BASE,
                        "event_sha": SHA}
            (delivery / "source-inputs.json").write_text(json.dumps(archived))
            ElementTree.ElementTree(ElementTree.fromstring(
                "<manifest><remote name='candidate' fetch='https://github.com/Embracecactus/'/>"
                "<project path='contest2026_135_yongwangzhiqian' "
                "name='contest2026_135_yongwangzhiqian' remote='candidate' "
                f"revision='{SHA}'/></manifest>"
            )).write(delivery / "declared-manifest.xml")
            with self.assertRaises(ValueError):
                source_module().verify_delivery(identity, delivery)

            pair = delivery / "pair"
            repository = pair / "repository"
            releases = pair / "releases/mcuboot"
            configs = pair / "configs/mcuboot"
            output = delivery / "staged"
            repository.mkdir(parents=True)
            releases.mkdir(parents=True)
            inputs = {}
            elfs = {}
            roles = {}
            maps = {"bl1": "bl.map", "bl2": "bl2.map",
                    "cp": "nuttx.map", "ap": "nuttx.map"}
            for index, role in enumerate(("bl1", "bl2", "cp", "ap")):
                directory = releases / role
                directory.mkdir()
                elf = directory / (role + ".elf")
                elf.write_bytes((role + "-elf").encode())
                elfs[role] = {"path": str(elf.relative_to(pair)),
                              "sha256": hashlib.sha256(elf.read_bytes()).hexdigest()}
                image = directory / maps[role]
                image.write_bytes((role + "-map").encode())
                if role in ("cp", "ap"):
                    config = directory / ".config"
                    config.write_bytes((role + "-config").encode())
                    seed = configs / role / "defconfig"
                    seed.parent.mkdir(parents=True)
                    seed.write_bytes((role + "-defconfig").encode())
                    roles[role] = {"resolved_config_sha256": hashlib.sha256(
                        config.read_bytes()).hexdigest()
                    }
                    roles[role]["seed_defconfig_sha256"] = hashlib.sha256(
                        seed.read_bytes()).hexdigest()
            for name in ("boot", "bl2", "cp", "ap"):
                image = releases / (name + ".bin")
                image.write_bytes((name + "-bin").encode())
                inputs[name] = {"path": str(image.relative_to(pair)),
                                "sha256": hashlib.sha256(image.read_bytes()).hexdigest()}
            partition = repository / "partitions.csv"
            partition.write_text("name,address,size\napp,0x1000,0x2000\n")
            manifest = {
                "elfs": elfs, "inputs": inputs, "roles": roles,
                "layout": {"partition": "partitions.csv"},
            }
            manifest_path = releases / "build-manifest.json"
            manifest_path.write_text(json.dumps(manifest))

            with self.subTest("stage exact matching evidence"):
                source_module().stage_build_evidence(manifest_path, repository, output)
                self.assertEqual(set(path.name for path in output.iterdir()), {
                    "bl1.elf", "bl2.elf", "cp.elf", "ap.elf", "boot.bin",
                    "bl2.bin", "cp.bin", "ap.bin", "bl1.map", "bl2.map",
                    "cp.map", "ap.map", "cp.config", "ap.config", "cp.defconfig",
                    "ap.defconfig", "partitions.csv",
                })
                self.assertEqual((output / "partitions.csv").read_bytes(),
                                 partition.read_bytes())
                for records, suffix in ((elfs, ".elf"), (inputs, ".bin")):
                    for name, record in records.items():
                        copied = output / (name + suffix)
                        self.assertEqual(hashlib.sha256(copied.read_bytes()).hexdigest(),
                                         record["sha256"])
                for role in ("cp", "ap"):
                    self.assertEqual((output / (role + ".config")).read_bytes(),
                                     (releases / role / ".config").read_bytes())
                    self.assertEqual((output / (role + ".defconfig")).read_bytes(),
                                     (configs / role / "defconfig").read_bytes())
                for role, name in maps.items():
                    self.assertEqual((output / (role + ".map")).read_bytes(),
                                     (releases / role / name).read_bytes())

            with self.subTest("independent evidence verification and product isolation"):
                public = delivery / "public"
                (public / "firmware/evidence").mkdir(parents=True)
                (public / "build-evidence").mkdir()
                for file in output.iterdir():
                    (public / "build-evidence" / file.name).write_bytes(file.read_bytes())
                public_manifest = public / "firmware/evidence/build-manifest.json"
                public_manifest.write_text(json.dumps(manifest))
                (public / "firmware/release.json").write_text(json.dumps({
                    "build_manifest": {"path": "evidence/build-manifest.json"}}))
                source_module().verify_build_evidence(public)
                copied = public / "build-evidence/cp.config"
                original = copied.read_bytes()
                copied.write_bytes(b"CONFIG_BK7258_ENGINEERING_TEST=y\n")
                with self.assertRaises(ValueError):
                    source_module().verify_build_evidence(public)
                changed = json.loads(public_manifest.read_text())
                changed["roles"]["cp"]["resolved_config_sha256"] = hashlib.sha256(
                    copied.read_bytes()).hexdigest()
                public_manifest.write_text(json.dumps(changed))
                with self.assertRaisesRegex(ValueError, "engineering"):
                    source_module().verify_build_evidence(public)
                copied.write_bytes(original)
                public_manifest.write_text(json.dumps(manifest))
                (public / "build-evidence/ap.map").unlink()
                with self.assertRaises(ValueError):
                    source_module().verify_build_evidence(public)

            with self.subTest("reject changed elf before copy"):
                bad_output = delivery / "changed-output"
                (releases / "bl1/bl1.elf").write_bytes(b"changed")
                with self.assertRaises(ValueError):
                    source_module().stage_build_evidence(manifest_path, repository,
                                                         bad_output)
                self.assertFalse((bad_output / "bl1.elf").exists())

            with self.subTest("reject pair-root escape before copy"):
                bad_output = delivery / "escape-output"
                manifest["elfs"]["bl1"]["path"] = "../outside.elf"
                manifest_path.write_text(json.dumps(manifest))
                with self.assertRaises(ValueError):
                    source_module().stage_build_evidence(manifest_path, repository,
                                                         bad_output)
                self.assertFalse((bad_output / "bl1.elf").exists())

    def test_workflow_pr_source_boundary_is_low_privilege_and_pre_sync(self):
        workflow = (ROOT / ".github/workflows/shaniu-source-checks.yml").read_text()
        self.assertIn("  pull_request:\n", workflow)
        self.assertIn('--event-checkout "$GITHUB_WORKSPACE"', workflow)
        self.assertIn("permissions:\n  contents: read\n", workflow)
        self.assertNotIn("pull_request_target", workflow)
        self.assertNotIn("heads/$GITHUB_REF_NAME", workflow)
        self.assertIn('-b "$GITHUB_REF"', workflow)
        self.assertNotIn("--manifest-upstream-branch", workflow)
        self.assertLess(workflow.index('test "$(git -C .repo/manifests rev-parse HEAD)" = "$GITHUB_SHA"'),
                        workflow.index("repo sync -j4"))
        self.assertNotIn("${{ secrets.", workflow)
        self.assertLess(workflow.index("shaniu_source.py\" override"),
                        workflow.index("repo sync -j4"))

    def test_verify_checkouts_requires_real_merge_parents_and_all_three_heads(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source = root / "source"; source.mkdir()
            git(source, "init", "-q", "-b", "master")
            git(source, "config", "user.name", "ci")
            git(source, "config", "user.email", "ci@example.invalid")
            base = commit(source, "base")
            git(source, "checkout", "-qb", "topic")
            head = commit(source, "head")
            git(source, "checkout", "-q", "master")
            git(source, "merge", "--no-ff", "topic", "-m", "merge")
            candidate = git(source, "rev-parse", "HEAD")
            identity = {"event": "pull_request", "candidate_sha": candidate,
                        "head_sha": head, "base_sha": base}
            manifest = root / "manifest"; event = root / "event"
            for checkout in (manifest, event):
                git(root, "clone", "-q", str(source), str(checkout))
                git(checkout, "checkout", "-q", candidate)
            result = source_module().verify_checkouts(identity, manifest, source, event)
            self.assertEqual(result["candidate_sha"], candidate)
            self.assertEqual(result["manifest_sha"], candidate)
            self.assertEqual(result["source_sha"], candidate)
            git(event, "checkout", "-q", base)
            with self.assertRaises(ValueError):
                source_module().verify_checkouts(identity, manifest, source, event)
            git(event, "checkout", "-q", candidate)
            git(manifest, "checkout", "-q", base)
            with self.assertRaises(ValueError):
                source_module().verify_checkouts(identity, manifest, source, event)
            git(manifest, "checkout", "-q", candidate)
            wrong_parents = {**identity, "base_sha": head, "head_sha": base}
            with self.assertRaises(ValueError):
                source_module().verify_checkouts(wrong_parents, manifest, source, event)
            git(source, "checkout", "-q", base)
            with self.assertRaises(ValueError):
                source_module().verify_checkouts(identity, manifest, source, event)


if __name__ == "__main__":
    unittest.main(verbosity=2)
