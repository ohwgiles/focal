### Contributing to focal

Thanks for your interest! Focal needs your help to reach its full potential.

The [Issues page](https://github.com/ohwgiles/focal/issues) is the main point of collaboration for this project. Please check there first if there is already an open issue that addresses your concern. Any issue tagged `help-wanted` is an invitation from the reporter for contribution. In other cases, feel free to open a new one - all types of issue or question are welcome.

To work on the code, fork it to your own GitHub account and request that your changes are merged back into the main repository using a [pull request](https://help.github.com/categories/collaborating-with-issues-and-pull-requests/).

Before starting to work on a bugfix or new feature, please clearly communicate this on the corresponding issue, and if it's not a trivial change, give others a chance to comment. Decisions should be taken based on merit and guided by the following principles:

- Added value: is the increased complexity of a new feature justified by its utility?
- Code quality: is the new code easily understandable, well encapsulated, (relatively) efficient and safe?
- Minimalism: focal should stay as reasonably lightweight as possible.

Don't worry, this is less complicated than it sounds. The main thing is to have a constructive, transparent development process.

Contributors to focal should have at least basic knowledge of C. Familiarity with GTK is helpful, as is experience writing object-oriented in general.

All issues should be labelled with `simple`, `moderate` or `challenging`; beginner contributers should start with a `simple` issue.

#### Commit style

Commits should be in the following format:

```
$ git commit -m"resolves #12345: implement easter egg
>
>More details about the commit."
```

#### Code style

The coding style is enforced by the `.clang-format` style file. Please run `clang-format -i *.c *.h` in the source directory before committing to automatically standardize the code style.

#### Licensing

Contributors retain copyright over their contributions, but by submitting a pull request agree to license their contributions under the same license as focal, i.e. the GPLv3.
