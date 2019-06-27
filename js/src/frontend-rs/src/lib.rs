#[no_mangle]
pub extern fn asdf() {
    println!("Hiiiii!");
}
#[cfg(test)]
mod tests {
    #[test]
    fn it_works() {
        assert_eq!(2 + 2, 4);
    }
}
