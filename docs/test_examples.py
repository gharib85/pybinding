import pytest
import pathlib


docs = pathlib.Path(__file__).parent
examples = [p for p in docs.glob('*/**/*.py') if "_ext" not in str(p)]
assert len(examples) != 0


@pytest.fixture(scope='module', ids=[e.stem for e in examples], params=examples)
def example_file(request):
    """An example file from the documentation directory"""
    return request.param


def test_docs(example_file):
    """Make sure all example files execute without error"""
    filename = str(example_file)
    exec(compile(open(filename, 'rb').read(), filename, 'exec'), {})
